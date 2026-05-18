#ifndef SQFS_MGR_H
#define SQFS_MGR_H

#include <dirent.h>
#include <stdbool.h>

void sqfs_mgr_unload_image(void);
bool sqfs_mgr_load_image(void);
int sqfs_open(const char *pathname, int flags, ...);
DIR *sqfs_opendir(const char *name);
int sqfs_closedir(DIR *dir);

#endif /* SQFS_MGR_H */
