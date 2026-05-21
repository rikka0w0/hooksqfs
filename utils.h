#ifndef UTILS_H
#define UTILS_H

#include <dirent.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct PtrHashMapEntry *PtrHashMap;

void map_put(PtrHashMap *map, void *key, void *value);
void *map_get(PtrHashMap map, void *key);
int map_delete(PtrHashMap *map, void *key);
void map_free_all(PtrHashMap *map);

int create_backing_fd(int flags);
DIR *create_backing_dir(void);

bool path_normalize_lexical(const char *in, char *out, size_t out_size);
bool path_equals_normalized(const char *a, const char *b);
/*
 * Pass NULL for relative_out and/or 0 for relative_out_size to only check
 * containment.
 */
bool path_relative_to_root(const char *root,
                           const char *path,
                           char *relative_out,
                           size_t relative_out_size);


#endif /* UTILS_H */
