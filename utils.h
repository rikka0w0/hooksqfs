#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdbool.h>

typedef struct PtrHashMapEntry *PtrHashMap;

void map_put(PtrHashMap *map, void *key, void *value);
void *map_get(PtrHashMap map, void *key);
int map_delete(PtrHashMap *map, void *key);
void map_free_all(PtrHashMap *map);

int create_backing_fd(int flags);

bool path_normalize_lexical(const char *in, char *out, size_t out_size);
bool path_equals_normalized(const char *a, const char *b);
bool path_relative_to_root(const char *root,
                           const char *path,
                           char *relative_out,
                           size_t relative_out_size);
bool path_is_under_prefix(const char* prefix, const char *path);


#endif /* UTILS_H */
