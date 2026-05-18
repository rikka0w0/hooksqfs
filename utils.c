#include "utils.h"
#include "uthash.h"
#include "logging.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static bool normalize_path_lexical(const char *in, char *out, size_t out_size)
{
	char tmp[PATH_MAX];
	char *parts[PATH_MAX / 2];
	size_t count = 0;
	char *p, *token;
	bool absolute;

	if (in == NULL || out == NULL || out_size == 0) {
		return false;
	}

	if (strlen(in) >= sizeof(tmp)) {
		return false;
	}

	strcpy(tmp, in);
	absolute = (in[0] == '/');

	p = tmp;

	while ((token = strsep(&p, "/")) != NULL) {
		if (token[0] == '\0' || strcmp(token, ".") == 0) {
			continue;
		}

		if (strcmp(token, "..") == 0) {
			if (count > 0 && strcmp(parts[count - 1], "..") != 0) {
				count--;
			} else if (!absolute) {
				parts[count++] = token;
			}
			continue;
		}

		parts[count++] = token;
	}

	out[0] = '\0';

	if (absolute) {
		strlcat(out, "/", out_size);
	}

	for (size_t i = 0; i < count; i++) {
		if (i > 0 || absolute) {
			if (strlen(out) > 1) {
				strlcat(out, "/", out_size);
			}
		}
		strlcat(out, parts[i], out_size);
	}

	if (out[0] == '\0') {
		strlcpy(out, absolute ? "/" : ".", out_size);
	}

	return true;
}

bool is_under_hooksqfs_prefix(const char *path)
{
	const char *prefix = getenv("HOOKSQFS_PREFIX");
	char norm_path[PATH_MAX];
	char norm_prefix[PATH_MAX];
	size_t prefix_len;
	size_t path_len;

	if (path == NULL || prefix == NULL || prefix[0] == '\0') {
		return false;
	}

	if (!normalize_path_lexical(path, norm_path, sizeof(norm_path)) ||
		!normalize_path_lexical(prefix, norm_prefix, sizeof(norm_prefix))) {
		return false;
	}

	prefix_len = strlen(norm_prefix);
	path_len = strlen(norm_path);

	while (prefix_len > 1 && norm_prefix[prefix_len - 1] == '/') {
		prefix_len--;
	}

	if (path_len == prefix_len &&
		strncmp(norm_path, norm_prefix, prefix_len) == 0) {
		return true;
	}

	if (path_len > prefix_len &&
		strncmp(norm_path, norm_prefix, prefix_len) == 0 &&
		norm_path[prefix_len] == '/') {
		return true;
	}

	return false;
}

