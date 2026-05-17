#define _GNU_SOURCE

#include "logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

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

	syscall(SYS_write, STDERR_FILENO, buf, (size_t)total);
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

	syscall(SYS_write, STDERR_FILENO, buf, (size_t)n);
}
