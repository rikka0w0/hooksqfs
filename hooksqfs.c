#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

/*
 * Avoid using printf/fprintf inside hooks.
 * They may call fopen/write internally and cause recursion.
 */
static void log_msg(const char *fmt, ...)
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

#define RESOLVE_REAL(name)                                           \
    do {                                                             \
        if (!real_##name) {                                          \
            real_##name = dlsym(RTLD_NEXT, #name);                   \
            if (!real_##name) {                                      \
                log_msg("[hooklog] dlsym failed: %s\n", #name);      \
                errno = ENOSYS;                                      \
                return -1;                                           \
            }                                                        \
        }                                                            \
    } while (0)

#define RESOLVE_REAL_PTR(name)                                       \
    do {                                                             \
        if (!real_##name) {                                          \
            real_##name = dlsym(RTLD_NEXT, #name);                   \
            if (!real_##name) {                                      \
                log_msg("[hooklog] dlsym failed: %s\n", #name);      \
                errno = ENOSYS;                                      \
                return NULL;                                         \
            }                                                        \
        }                                                            \
    } while (0)

/* ------------------------------------------------------------------ */
/* open / open64                                                       */
/* ------------------------------------------------------------------ */

static int (*real_open)(const char *pathname, int flags, ...) = NULL;
static int (*real_open64)(const char *pathname, int flags, ...) = NULL;

static int flags_need_mode(int flags)
{
#ifdef O_TMPFILE
    if ((flags & O_TMPFILE) == O_TMPFILE)
        return 1;
#endif
    return (flags & O_CREAT) != 0;
}

int open(const char *pathname, int flags, ...)
{
    RESOLVE_REAL(open);

    mode_t mode = 0;
    int has_mode = flags_need_mode(flags);

    if (has_mode) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);

        log_msg("[hooklog] open(path=\"%s\", flags=0x%x, mode=%04o)\n",
                pathname ? pathname : "(null)", flags, mode);

        return real_open(pathname, flags, mode);
    } else {
        log_msg("[hooklog] open(path=\"%s\", flags=0x%x)\n",
                pathname ? pathname : "(null)", flags);

        return real_open(pathname, flags);
    }
}

int open64(const char *pathname, int flags, ...)
{
    RESOLVE_REAL(open64);

    mode_t mode = 0;
    int has_mode = flags_need_mode(flags);

    if (has_mode) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);

        log_msg("[hooklog] open64(path=\"%s\", flags=0x%x, mode=%04o)\n",
                pathname ? pathname : "(null)", flags, mode);

        return real_open64(pathname, flags, mode);
    } else {
        log_msg("[hooklog] open64(path=\"%s\", flags=0x%x)\n",
                pathname ? pathname : "(null)", flags);

        return real_open64(pathname, flags);
    }
}

/* ------------------------------------------------------------------ */
/* openat / openat64                                                   */
/* ------------------------------------------------------------------ */

static int (*real_openat)(int dirfd, const char *pathname, int flags, ...) = NULL;
static int (*real_openat64)(int dirfd, const char *pathname, int flags, ...) = NULL;

int openat(int dirfd, const char *pathname, int flags, ...)
{
    RESOLVE_REAL(openat);

    mode_t mode = 0;
    int has_mode = flags_need_mode(flags);

    if (has_mode) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);

        log_msg("[hooklog] openat(dirfd=%d, path=\"%s\", flags=0x%x, mode=%04o)\n",
                dirfd, pathname ? pathname : "(null)", flags, mode);

        return real_openat(dirfd, pathname, flags, mode);
    } else {
        log_msg("[hooklog] openat(dirfd=%d, path=\"%s\", flags=0x%x)\n",
                dirfd, pathname ? pathname : "(null)", flags);

        return real_openat(dirfd, pathname, flags);
    }
}

int openat64(int dirfd, const char *pathname, int flags, ...)
{
    RESOLVE_REAL(openat64);

    mode_t mode = 0;
    int has_mode = flags_need_mode(flags);

    if (has_mode) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);

        log_msg("[hooklog] openat64(dirfd=%d, path=\"%s\", flags=0x%x, mode=%04o)\n",
                dirfd, pathname ? pathname : "(null)", flags, mode);

        return real_openat64(dirfd, pathname, flags, mode);
    } else {
        log_msg("[hooklog] openat64(dirfd=%d, path=\"%s\", flags=0x%x)\n",
                dirfd, pathname ? pathname : "(null)", flags);

        return real_openat64(dirfd, pathname, flags);
    }
}

/* ------------------------------------------------------------------ */
/* fopen / fopen64                                                     */
/* ------------------------------------------------------------------ */

static FILE *(*real_fopen)(const char *pathname, const char *mode) = NULL;
static FILE *(*real_fopen64)(const char *pathname, const char *mode) = NULL;

FILE *fopen(const char *pathname, const char *mode)
{
    RESOLVE_REAL_PTR(fopen);

    log_msg("[hooklog] fopen(path=\"%s\", mode=\"%s\")\n",
            pathname ? pathname : "(null)",
            mode ? mode : "(null)");

    return real_fopen(pathname, mode);
}

FILE *fopen64(const char *pathname, const char *mode)
{
    RESOLVE_REAL_PTR(fopen64);

    log_msg("[hooklog] fopen64(path=\"%s\", mode=\"%s\")\n",
            pathname ? pathname : "(null)",
            mode ? mode : "(null)");

    return real_fopen64(pathname, mode);
}

/* ------------------------------------------------------------------ */
/* access / faccessat                                                  */
/* ------------------------------------------------------------------ */

static int (*real_access)(const char *pathname, int mode) = NULL;
static int (*real_faccessat)(int dirfd, const char *pathname, int mode, int flags) = NULL;

int access(const char *pathname, int mode)
{
    RESOLVE_REAL(access);

    log_msg("[hooklog] access(path=\"%s\", mode=0x%x)\n",
            pathname ? pathname : "(null)", mode);

    return real_access(pathname, mode);
}

int faccessat(int dirfd, const char *pathname, int mode, int flags)
{
    RESOLVE_REAL(faccessat);

    log_msg("[hooklog] faccessat(dirfd=%d, path=\"%s\", mode=0x%x, flags=0x%x)\n",
            dirfd, pathname ? pathname : "(null)", mode, flags);

    return real_faccessat(dirfd, pathname, mode, flags);
}

/* ------------------------------------------------------------------ */
/* glibc stat family, important for 32-bit x86                         */
/* ------------------------------------------------------------------ */

static int (*real___xstat)(int ver, const char *pathname, struct stat *buf) = NULL;
static int (*real___lxstat)(int ver, const char *pathname, struct stat *buf) = NULL;
static int (*real___fxstat)(int ver, int fd, struct stat *buf) = NULL;

int __xstat(int ver, const char *pathname, struct stat *buf)
{
    RESOLVE_REAL(__xstat);

    log_msg("[hooklog] __xstat(ver=%d, path=\"%s\")\n",
            ver, pathname ? pathname : "(null)");

    return real___xstat(ver, pathname, buf);
}

int __lxstat(int ver, const char *pathname, struct stat *buf)
{
    RESOLVE_REAL(__lxstat);

    log_msg("[hooklog] __lxstat(ver=%d, path=\"%s\")\n",
            ver, pathname ? pathname : "(null)");

    return real___lxstat(ver, pathname, buf);
}

int __fxstat(int ver, int fd, struct stat *buf)
{
    RESOLVE_REAL(__fxstat);

    log_msg("[hooklog] __fxstat(ver=%d, fd=%d)\n", ver, fd);

    return real___fxstat(ver, fd, buf);
}

#ifdef __USE_LARGEFILE64
static int (*real___xstat64)(int ver, const char *pathname, struct stat64 *buf) = NULL;
static int (*real___lxstat64)(int ver, const char *pathname, struct stat64 *buf) = NULL;
static int (*real___fxstat64)(int ver, int fd, struct stat64 *buf) = NULL;

int __xstat64(int ver, const char *pathname, struct stat64 *buf)
{
    RESOLVE_REAL(__xstat64);

    log_msg("[hooklog] __xstat64(ver=%d, path=\"%s\")\n",
            ver, pathname ? pathname : "(null)");

    return real___xstat64(ver, pathname, buf);
}

int __lxstat64(int ver, const char *pathname, struct stat64 *buf)
{
    RESOLVE_REAL(__lxstat64);

    log_msg("[hooklog] __lxstat64(ver=%d, path=\"%s\")\n",
            ver, pathname ? pathname : "(null)");

    return real___lxstat64(ver, pathname, buf);
}

int __fxstat64(int ver, int fd, struct stat64 *buf)
{
    RESOLVE_REAL(__fxstat64);

    log_msg("[hooklog] __fxstat64(ver=%d, fd=%d)\n", ver, fd);

    return real___fxstat64(ver, fd, buf);
}
#endif
