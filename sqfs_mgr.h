#ifndef SQFS_MGR_H
#define SQFS_MGR_H

#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

void sqfs_mutex_init_once(void);
bool sqfs_mgr_load_image(void);
int sqfs_open(const char *pathname, int flags, ...);
ssize_t sqfs_read(int fd, void *buf, size_t count);
ssize_t sqfs_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t sqfs_pread64(int fd, void *buf, size_t count, off64_t offset);
off_t sqfs_lseek(int fd, off_t offset, int whence);
off64_t sqfs_lseek64(int fd, off64_t offset, int whence);
int sqfs_close(int fd);
int sqfs_close_nocancel(int fd);
int sqfs_xstat(int vers, const char *pathname, struct stat *buf);
int sqfs_access(const char *pathname, int mode);
DIR *sqfs_opendir(const char *name);
struct dirent *sqfs_readdir(DIR *dirp);
int sqfs_closedir(DIR *dir);

#endif /* SQFS_MGR_H */
