#include "sqfs_mgr.h"
#include "utils.h"
#include "logging.h"

#include <stdlib.h>
#include <limits.h>

static PtrHashMap g_pxMapDIR = NULL;
static PtrHashMap* qwq = &g_pxMapDIR;

static void map_test(void) {
	int a = 123;
	int b = 456;
	int c = 789;

	void *key = &a;
	void *value = &b;

	/* 插入 */
	map_put(qwq, key, value);

	/* 查询 */
	int *found = map_get(g_pxMapDIR, key);
	if (found != NULL) {
		log_msg("found: %d\n", *found);
	} else {
		log_msg("not found\n");
	}

	/* 更新同一个 key */
	map_put(qwq, key, &c);

	found = map_get(g_pxMapDIR, key);
	if (found != NULL) {
		log_msg("after update: %d\n", *found);
	}

	/* 删除 */
	if (map_delete(&g_pxMapDIR, key)) {
		log_msg("deleted\n");
	} else {
		log_msg("delete failed: not found\n");
	}

	/* 删除后再查询 */
	found = map_get(g_pxMapDIR, key);
	if (found != NULL) {
		log_msg("after delete: %d\n", *found);
	} else {
		log_msg("after delete: not found\n");
	}

	map_free_all(&g_pxMapDIR);
}

static void test_is_under_hooksqfs_prefix(void) {
	const char *prefix = getenv("HOOKSQFS_PREFIX");
	if (prefix == NULL) {
		prefix = "/hook";
	}
	const char *paths[] = {
		"/other/../hook/somefile",
		"/hook/somepath/../somefile",
		"/hook/somepath/somefile",
		"/hook/somefile",
		"/hook/",
		"/hook",
		"/other/somefile",
		NULL
	};

	for (int i = 0; paths[i] != NULL; i++) {
		log_msg("path_is_under_prefix('%s'): %s\n", paths[i],
				path_is_under_prefix(prefix, paths[i]) ? "true" : "false");
		char rel[PATH_MAX];
		if (path_relative_to_root(prefix, paths[i], rel, sizeof(rel))) {
			log_msg("  relative: %s\n", rel);
		} else {
			log_msg("  not relative to root\n");
		}
	}
}

int main(void)
{
	// extern void test_sqfs_listing(void);
	// test_sqfs_listing();
	sqfs_mgr_load_image();
	//map_test();
	test_is_under_hooksqfs_prefix();
	return 0;
}
