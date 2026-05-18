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

static bool append_str(char *out, size_t out_size, const char *s)
{
	size_t out_len;
	size_t s_len;

	if (out == NULL || s == NULL || out_size == 0) {
		return false;
	}

	out_len = strlen(out);
	s_len = strlen(s);

	if (s_len >= out_size - out_len) {
		return false;
	}

	memcpy(out + out_len, s, s_len + 1);
	return true;
}

bool path_normalize_lexical(const char *in, char *out, size_t out_size)
{
	char tmp[PATH_MAX];
	char *parts[PATH_MAX / 2];
	size_t count = 0;
	char *p;
	char *token;
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

	/*
	 * Split the path by '/', then process '.', '..', and empty components.
	 * This is lexical normalization only. It does not resolve symlinks.
	 */
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

	/*
	 * Rebuild the normalized path.
	 */
	if (absolute) {
		if (!append_str(out, out_size, "/")) {
			return false;
		}
	}

	for (size_t i = 0; i < count; i++) {
		if (i > 0 || absolute) {
			if (strlen(out) > 1) {
				if (!append_str(out, out_size, "/")) {
					return false;
				}
			}
		}

		if (!append_str(out, out_size, parts[i])) {
			return false;
		}
	}

	/*
	 * Normalize empty paths.
	 */
	if (out[0] == '\0') {
		if (!append_str(out, out_size, absolute ? "/" : ".")) {
			return false;
		}
	}

	return true;
}

bool path_relative_to_root(const char *root,
                           const char *path,
                           char *relative_out,
                           size_t relative_out_size)
{
	char norm_root[PATH_MAX];
	char norm_path[PATH_MAX];
	size_t root_len;
	size_t path_len;
	const char *relative;

	if (root == NULL || path == NULL ||
	    relative_out == NULL || relative_out_size == 0) {
		return false;
	}

	if (!path_normalize_lexical(root, norm_root, sizeof(norm_root)) ||
	    !path_normalize_lexical(path, norm_path, sizeof(norm_path))) {
		return false;
	}

	root_len = strlen(norm_root);
	path_len = strlen(norm_path);

	/*
	 * Strip trailing slashes from root, but keep "/" as-is.
	 */
	while (root_len > 1 && norm_root[root_len - 1] == '/') {
		root_len--;
	}

	/*
	 * If root is "/", every absolute path is under it.
	 */
	if (root_len == 1 && norm_root[0] == '/') {
		if (norm_path[0] != '/') {
			return false;
		}

		relative = norm_path + 1;

		if (strlen(relative) >= relative_out_size) {
			return false;
		}

		strcpy(relative_out, relative);
		return true;
	}

	/*
	 * The normalized path must start with the normalized root.
	 */
	if (path_len < root_len ||
	    strncmp(norm_path, norm_root, root_len) != 0) {
		return false;
	}

	/*
	 * The path is exactly equal to root.
	 */
	if (path_len == root_len) {
		if (relative_out_size < 1) {
			return false;
		}

		relative_out[0] = '\0';
		return true;
	}

	/*
	 * The next character must be '/', otherwise this would incorrectly
	 * match paths such as:
	 *
	 * root: /hook/somefolder
	 * path: /hook/somefolder2/file
	 */
	if (norm_path[root_len] != '/') {
		return false;
	}

	relative = norm_path + root_len + 1;

	if (strlen(relative) >= relative_out_size) {
		return false;
	}

	strcpy(relative_out, relative);
	return true;
}

bool path_is_under_prefix(const char* prefix, const char *path)
{
	char norm_path[PATH_MAX];
	char norm_prefix[PATH_MAX];
	size_t prefix_len;
	size_t path_len;

	if (path == NULL || prefix == NULL || prefix[0] == '\0') {
		return false;
	}

	if (!path_normalize_lexical(path, norm_path, sizeof(norm_path)) ||
		!path_normalize_lexical(prefix, norm_prefix, sizeof(norm_prefix))) {
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

