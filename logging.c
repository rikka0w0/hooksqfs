#define _GNU_SOURCE

#include "logging.h"
#include "real.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WRITE_LOG_TO_FILE 1

#if WRITE_LOG_TO_FILE
static void write_log_file(const char *buf, size_t len)
{
	if (g_LibcFuncs.open == NULL || g_LibcFuncs.write == NULL || g_LibcFuncs.close == NULL)
		return;

	int fd = g_LibcFuncs.open("/tmp/log.txt",
				   O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
				   0644);
	if (fd < 0)
		return;

	const char *p = buf;
	while (len > 0) {
		ssize_t written = g_LibcFuncs.write(fd, p, len);
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

	g_LibcFuncs.close(fd);
}
#endif

static int should_log(const char *func_name)
{
	const char *exclude = getenv("HOOKSQFS_LOG_EXCLUDE");
	if (!exclude || !func_name || !*func_name)
		return 1;

	const char *p = exclude;
	size_t name_len = strlen(func_name);

	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;

		const char *start = p;
		while (*p && *p != ',')
			p++;

		size_t token_len = p - start;
		while (token_len > 0 && (start[token_len - 1] == ' ' || start[token_len - 1] == '\t'))
			token_len--;

		if (token_len == name_len && strncmp(start, func_name, name_len) == 0)
			return 0;

		if (*p == ',')
			p++;
	}

	return 1;
}

void log_hook(const char *func_name, const char *fmt, ...)
{
	if (!should_log(func_name))
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

#if WRITE_LOG_TO_FILE
	write_log_file(buf, (size_t)total);
#else
	fputs(buf, stdout);
#endif
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

#if WRITE_LOG_TO_FILE
	write_log_file(buf, (size_t)n);
#else
	fputs(buf, stdout);
#endif
}
