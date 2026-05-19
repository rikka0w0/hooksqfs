#define _GNU_SOURCE

#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include "logging.h"
#include "sqfs_mgr.h"
#include "real.h"

/* ------------------------------------------------------------------ */
/* open / open64                                                       */
/* ------------------------------------------------------------------ */

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
	real_populate();

	mode_t mode = 0;
	int has_mode = flags_need_mode(flags);

	if (has_mode) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);

		int sqfs_fd = sqfs_open(pathname, flags, mode);
		if (sqfs_fd >= 0) {
			log_hook(__func__, "path=\"%s\", flags=0x%x, mode=%04o, sqfs_fd=%d\n",
					pathname , flags, mode, sqfs_fd);
			return sqfs_fd;
		}
		if (errno != ENOENT)
			return -1;

		log_hook(__func__, "path=\"%s\", flags=0x%x, mode=%04o\n",
				pathname , flags, mode);

		return real.open(pathname, flags, mode);
	} else {
		int sqfs_fd = sqfs_open(pathname, flags);
		if (sqfs_fd >= 0) {
			log_hook(__func__, "path=\"%s\", flags=0x%x, sqfs_fd=%d\n",
					pathname , flags, sqfs_fd);
			return sqfs_fd;
		}
		if (errno != ENOENT)
			return -1;

		log_hook(__func__, "path=\"%s\", flags=0x%x\n",
				pathname , flags);

		return real.open(pathname, flags);
	}
}

int open64(const char *pathname, int flags, ...)
{
	real_populate();

	mode_t mode = 0;
	int has_mode = flags_need_mode(flags);

	if (has_mode) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);

		int sqfs_fd = sqfs_open(pathname, flags, mode);
		if (sqfs_fd >= 0) {
			log_hook(__func__, "path=\"%s\", flags=0x%x, mode=%04o, sqfs_fd=%d\n",
					pathname , flags, mode, sqfs_fd);
			return sqfs_fd;
		}
		if (errno != ENOENT)
			return -1;

		log_hook(__func__, "path=\"%s\", flags=0x%x, mode=%04o\n",
				pathname , flags, mode);

		return real.open64(pathname, flags, mode);
	} else {
		int sqfs_fd = sqfs_open(pathname, flags);
		if (sqfs_fd >= 0) {
			log_hook(__func__, "path=\"%s\", flags=0x%x, sqfs_fd=%d\n",
					pathname , flags, sqfs_fd);
			return sqfs_fd;
		}
		if (errno != ENOENT)
			return -1;

		log_hook(__func__, "path=\"%s\", flags=0x%x\n",
				pathname , flags);

		return real.open64(pathname, flags);
	}
}

/* ------------------------------------------------------------------ */
/* openat / openat64                                                   */
/* ------------------------------------------------------------------ */

int openat(int dirfd, const char *pathname, int flags, ...)
{
	real_populate();

	mode_t mode = 0;
	int has_mode = flags_need_mode(flags);

	if (has_mode) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);

log_hook(__func__, "dirfd=%d, path=\"%s\", flags=0x%x, mode=%04o\n",
				dirfd, pathname , flags, mode);

		return real.openat(dirfd, pathname, flags, mode);
	} else {
log_hook(__func__, "dirfd=%d, path=\"%s\", flags=0x%x\n",
				dirfd, pathname , flags);

		return real.openat(dirfd, pathname, flags);
	}
}

int openat64(int dirfd, const char *pathname, int flags, ...)
{
	real_populate();

	mode_t mode = 0;
	int has_mode = flags_need_mode(flags);

	if (has_mode) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);

log_hook(__func__, "dirfd=%d, path=\"%s\", flags=0x%x, mode=%04o\n",
				dirfd, pathname , flags, mode);

		return real.openat64(dirfd, pathname, flags, mode);
	} else {
log_hook(__func__, "dirfd=%d, path=\"%s\", flags=0x%x\n",
				dirfd, pathname , flags);

		return real.openat64(dirfd, pathname, flags);
	}
}

/* ------------------------------------------------------------------ */
/* fopen / fopen64                                                     */
/* ------------------------------------------------------------------ */

FILE *fopen(const char *pathname, const char *mode)
{
	real_populate();

	log_hook(__func__, "path=\"%s\", mode=\"%s\"\n",
			pathname, mode );

	return real.fopen(pathname, mode);
}

FILE *fopen64(const char *pathname, const char *mode)
{
	real_populate();

	log_hook(__func__, "path=\"%s\", mode=\"%s\"\n",
			pathname, mode);

	return real.fopen64(pathname, mode);
}

/* ------------------------------------------------------------------ */
/* opendir/readdir/closedir                                            */
/* ------------------------------------------------------------------ */

DIR *opendir(const char *name)
{
	real_populate();

	DIR *sqfs_dir = sqfs_opendir(name);
	if (sqfs_dir != NULL) {
		log_hook(__func__, "path=\"%s\", sqfs_dir=0x%p\n",
			 name, (void *)sqfs_dir);
		return sqfs_dir;
	}
	if (errno != ENOENT)
		return NULL;

	log_hook(__func__, "path=\"%s\"\n", name);

	return real.opendir(name);
}

DIR *opendir64(const char *name)
{
	real_populate();

	DIR *sqfs_dir = sqfs_opendir(name);
	if (sqfs_dir != NULL) {
		log_hook(__func__, "path=\"%s\", sqfs_dir=0x%p\n",
			 name, (void *)sqfs_dir);
		return sqfs_dir;
	}
	if (errno != ENOENT)
		return NULL;

	log_hook(__func__, "path=\"%s\"\n", name);

	return real.opendir64(name);
}

DIR *fdopendir(int fd)
{
	real_populate();

	log_hook(__func__, "fd=%d\n", fd);

	return real.fdopendir(fd);
}

struct dirent *readdir(DIR *dirp)
{
	real_populate();

	struct dirent *entry = sqfs_readdir(dirp);
	if (entry != NULL) {
		return entry;
	}

	entry = real.readdir(dirp);
	log_hook(__func__, "%s\n", entry ? entry->d_name : "(null)");

	return entry;
}

struct dirent64 *readdir64(DIR *dirp)
{
	real_populate();

	struct dirent64 * entry = real.readdir64(dirp);
	log_hook(__func__, "%s\n", entry ? entry->d_name : "(null)");

	return entry;
}

int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result)
{
	real_populate();

	log_hook(__func__, "dir=0x%p, entry=0x%p\n",
			(void *)dirp, (void *)entry);

	return real.readdir_r(dirp, entry, result);
}

int readdir64_r(DIR *dirp, struct dirent64 *entry, struct dirent64 **result)
{
	real_populate();

	log_hook(__func__, "dir=0x%p, entry=0x%p\n",
			(void *)dirp, (void *)entry);

	return real.readdir64_r(dirp, entry, result);
}

int closedir(DIR *dirp)
{
	real_populate();

	int saved_errno = errno;
	int sqfs_ret = sqfs_closedir(dirp);
	if (sqfs_ret == 0) {
		log_hook(__func__, "dir=0x%p, sqfs\n", (void *)dirp);
		return 0;
	}
	if (errno != saved_errno)
		return -1;
	errno = saved_errno;

	log_hook(__func__, "dir=0x%p\n", (void *)dirp);

	return real.closedir(dirp);
}

/* ------------------------------------------------------------------ */
/* access / faccessat                                                  */
/* ------------------------------------------------------------------ */

int access(const char *pathname, int mode)
{
	real_populate();

	log_hook(__func__, "path=\"%s\", mode=0x%x\n",
		pathname, mode);

	return real.access(pathname, mode);
}

int faccessat(int dirfd, const char *pathname, int mode, int flags)
{
	real_populate();

	log_hook(__func__, "dirfd=%d, path=\"%s\", mode=0x%x, flags=0x%x\n",
			dirfd, pathname, mode, flags);

	return real.faccessat(dirfd, pathname, mode, flags);
}

/* ------------------------------------------------------------------ */
/* glibc stat family, important for 32-bit x86                         */
/* ------------------------------------------------------------------ */

int __xstat(int ver, const char *pathname, struct stat *buf)
{
	real_populate();

	log_hook(__func__, "ver=%d, path=\"%s\"\n",
			ver, pathname ? pathname : "(null)");

	return real.__xstat(ver, pathname, buf);
}

int __lxstat(int ver, const char *pathname, struct stat *buf)
{
	real_populate();

	log_hook(__func__, "ver=%d, path=\"%s\"\n",
			ver, pathname ? pathname : "(null)");

	return real.__lxstat(ver, pathname, buf);
}

int __fxstat(int ver, int fd, struct stat *buf)
{
	real_populate();

	log_hook(__func__, "ver=%d, fd=%d\n", ver, fd);

	return real.__fxstat(ver, fd, buf);
}

#undef __USE_LARGEFILE64
#ifdef __USE_LARGEFILE64
int __xstat64(int ver, const char *pathname, struct stat64 *buf)
{
	real_populate();

	log_hook(__func__, "ver=%d, path=\"%s\"\n",
			ver, pathname ? pathname : "(null)");

	return real.__xstat64(ver, pathname, buf);
}

int __lxstat64(int ver, const char *pathname, struct stat64 *buf)
{
	real_populate();

	log_hook(__func__, "ver=%d, path=\"%s\"\n",
			ver, pathname ? pathname : "(null)");

	return real.__lxstat64(ver, pathname, buf);
}

int __fxstat64(int ver, int fd, struct stat64 *buf)
{
	real_populate();

	log_hook(__func__, "ver=%d, fd=%d\n", ver, fd);

	return real.__fxstat64(ver, fd, buf);
}
#endif
