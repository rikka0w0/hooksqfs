#ifndef SQFS_MGR_H
#define SQFS_MGR_H

#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

void sqfs_mgr_unload_image(void);
bool sqfs_mgr_load_image(void);
int sqfs_open(const char *pathname, int flags, ...);
ssize_t sqfs_read(int fd, void *buf, size_t count);
int sqfs_close(int fd);
int sqfs_xstat(int vers, const char *pathname, struct stat *buf);
int sqfs_access(const char *pathname, int mode);
DIR *sqfs_opendir(const char *name);
struct dirent *sqfs_readdir(DIR *dirp);
int sqfs_closedir(DIR *dir);

#endif /* SQFS_MGR_H */
