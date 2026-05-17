#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>

typedef struct PtrHashMapEntry *PtrHashMap;

void map_put(PtrHashMap *map, void *key, void *value);
void *map_get(PtrHashMap map, void *key);
int map_delete(PtrHashMap *map, void *key);
void map_free_all(PtrHashMap *map);

bool is_under_hooksqfs_prefix(const char *path);

#endif /* UTILS_H */