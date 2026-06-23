#define _GNU_SOURCE

#include "logging.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static pthread_once_t g_xLogOutputOnce = PTHREAD_ONCE_INIT;
static int g_iLogOutputFd = -1;

static void log_output_init_once(void)
{
	const char *path = getenv("HOOKSQFS_LOG_FILE");

	if (path && path[0]) {
		g_iLogOutputFd = (int)syscall(SYS_openat, AT_FDCWD, path,
					      O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
					      0644);
		return;
	}

	g_iLogOutputFd = STDOUT_FILENO;
}

static void log_write(const char *buf, size_t len)
{
	pthread_once(&g_xLogOutputOnce, log_output_init_once);
	if (g_iLogOutputFd < 0)
		return;

	const char *p = buf;
	while (len > 0) {
		ssize_t written = (ssize_t)syscall(SYS_write, g_iLogOutputFd, p, len);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (written == 0)
			break;

		p += written;
		len -= (size_t)written;
	}
}

int log_enabled(const char *func_name)
{
	const char *include = getenv("HOOKSQFS_LOG_INCLUDE");
	if (!include || !*include)
		return 0;

	const char *p = include;
	size_t name_len = func_name ? strlen(func_name) : 0;

	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;

		const char *start = p;
		while (*p && *p != ',')
			p++;

		size_t token_len = p - start;
		while (token_len > 0 && (start[token_len - 1] == ' ' || start[token_len - 1] == '\t'))
			token_len--;

		if (token_len == 3 && strncmp(start, "ALL", 3) == 0)
			return 1;

		if (func_name && *func_name && token_len == name_len &&
		    strncmp(start, func_name, name_len) == 0)
			return 1;

		if (func_name && strncmp(func_name, "sqfs_", 5) == 0) {
			const char *short_name = func_name + 5;
			size_t short_len = strlen(short_name);

			if (token_len == short_len && strncmp(start, short_name, short_len) == 0)
				return 1;
		}

		if (*p == ',')
			p++;
	}

	return 0;
}

void log_hook(const char *func_name, const char *fmt, ...)
{
	if (!log_enabled(func_name))
		return;

	char buf[1024];
	int prefix_len = 0;

	if (func_name && func_name[0]) {
		prefix_len = snprintf(buf, sizeof(buf), "[hooklog] %s: ", func_name);
		if (prefix_len < 0 || prefix_len >= (int)sizeof(buf))
			return;
	}

	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf + prefix_len, sizeof(buf) - prefix_len, fmt, ap);
	va_end(ap);

	if (n <= 0)
		return;

	int total = prefix_len + n;
	if (total > (int)sizeof(buf))
		total = sizeof(buf);

	log_write(buf, (size_t)total);
}

void log_msg(const char *fmt, ...)
{
	char buf[1024];

	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (n <= 0)
		return;

	if (n > (int)sizeof(buf))
		n = sizeof(buf);

	log_write(buf, (size_t)n);
}
