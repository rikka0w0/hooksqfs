#ifndef SQFS_MGR_H
#define SQFS_MGR_H

#include <dirent.h>
#include <stdbool.h>
#include <sys/stat.h>

void sqfs_mgr_unload_image(void);
bool sqfs_mgr_load_image(void);
int sqfs_open(const char *pathname, int flags, ...);
int sqfs_xstat(int vers, const char *pathname, struct stat *buf);
int sqfs_access(const char *pathname, int mode);
DIR *sqfs_opendir(const char *name);
struct dirent *sqfs_readdir(DIR *dirp);
int sqfs_closedir(DIR *dir);

#endif /* SQFS_MGR_H */
