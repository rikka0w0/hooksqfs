#include "utils.h"
#include "uthash.h"
#include "logging.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct PtrHashMapEntry {
	void *key;
	void *value;
	UT_hash_handle hh;
};

void map_put(PtrHashMap *map, void *key, void *value) {
	struct PtrHashMapEntry *entry = NULL;

	HASH_FIND_PTR(*map, &key, entry);
	if (entry == NULL) {
		entry = malloc(sizeof(*entry));
		if (entry == NULL) {
			exit(1);
		}

		entry->key = key;
		HASH_ADD_PTR(*map, key, entry);
	}

	entry->value = value;
}

void *map_get(PtrHashMap map, void *key) {
	struct PtrHashMapEntry *entry = NULL;

	HASH_FIND_PTR(map, &key, entry);
	return entry ? entry->value : NULL;
}

int map_delete(PtrHashMap *map, void *key) {
	struct PtrHashMapEntry *entry = NULL;

	HASH_FIND_PTR(*map, &key, entry);
	if (entry == NULL) {
		return 0; /* 没找到 */
	}

	HASH_DEL(*map, entry);
	free(entry);
	return 1; /* 删除成功 */
}

void map_free_all(PtrHashMap *map) {
	struct PtrHashMapEntry *entry = NULL;
	struct PtrHashMapEntry *tmp = NULL;

	HASH_ITER(hh, *map, entry, tmp) {
		HASH_DEL(*map, entry);
		free(entry);
	}
}

bool is_under_hooksqfs_prefix(const char *path)
{
	const char *prefix = getenv("HOOKSQFS_PREFIX");
	size_t prefix_len;
	size_t path_len;

	if (path == NULL || prefix == NULL || prefix[0] == '\0') {
		return false;
	}

	prefix_len = strlen(prefix);
	path_len = strlen(path);

	/*
	 * Strip trailing slashes from HOOKSQFS_PREFIX,
	 * but keep "/" as-is.
	 */
	while (prefix_len > 1 && prefix[prefix_len - 1] == '/') {
		prefix_len--;
	}

	/*
	 * path is exactly equal to prefix.
	 */
	if (path_len == prefix_len && strncmp(path, prefix, prefix_len) == 0) {
		return true;
	}

	/*
	 * path is under prefix:
	 * it must start with prefix followed by '/'.
	 */
	if (path_len > prefix_len &&
		strncmp(path, prefix, prefix_len) == 0 &&
		path[prefix_len] == '/') {
		return true;
	}

	return false;
}
