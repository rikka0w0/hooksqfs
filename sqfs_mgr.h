#ifndef SQFS_MGR_H
#define SQFS_MGR_H

#include <stdbool.h>

void sqfs_mgr_unload_image(void);
bool sqfs_mgr_load_image(void);
int sqfs_open(const char *pathname, int flags, ...);

#endif /* SQFS_MGR_H */
