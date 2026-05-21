#ifndef REAL_H
#define REAL_H

#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

struct LibcFunctions {
	int (*open)(const char *pathname, int flags, ...);
	int (*open64)(const char *pathname, int flags, ...);
	ssize_t (*write)(int fd, const void *buf, size_t count);
	int (*close)(int fd);
	int (*openat)(int dirfd, const char *pathname, int flags, ...);
	int (*openat64)(int dirfd, const char *pathname, int flags, ...);
	FILE *(*fopen)(const char *pathname, const char *mode);
	FILE *(*fopen64)(const char *pathname, const char *mode);
	DIR *(*opendir)(const char *name);
	DIR *(*opendir64)(const char *name);
	DIR *(*fdopendir)(int fd);
	struct dirent *(*readdir)(DIR *dirp);
	struct dirent64 *(*readdir64)(DIR *dirp);
	int (*readdir_r)(DIR *dirp, struct dirent *entry, struct dirent **result);
	int (*readdir64_r)(DIR *dirp, struct dirent64 *entry, struct dirent64 **result);
	int (*closedir)(DIR *dirp);
	int (*scandir)(const char *dirp, struct dirent ***namelist,
		       int (*filter)(const struct dirent *),
		       int (*compar)(const struct dirent **, const struct dirent **));
	int (*access)(const char *pathname, int mode);
	int (*faccessat)(int dirfd, const char *pathname, int mode, int flags);
	int (*__xstat)(int ver, const char *pathname, struct stat *buf);
	int (*__lxstat)(int ver, const char *pathname, struct stat *buf);
	int (*__fxstat)(int ver, int fd, struct stat *buf);
#ifdef __USE_LARGEFILE64
	int (*__xstat64)(int ver, const char *pathname, struct stat64 *buf);
	int (*__lxstat64)(int ver, const char *pathname, struct stat64 *buf);
	int (*__fxstat64)(int ver, int fd, struct stat64 *buf);
#endif
};

extern struct LibcFunctions g_LibcFuncs;
extern struct LibcFunctions g_xTrampoline;

void vPopulateLibcFuncPtrs(void);

#endif /* REAL_H */
