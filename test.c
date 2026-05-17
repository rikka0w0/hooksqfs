#include "sqfs_file.h"
#include "utils.h"
#include "logging.h"

#include <stdlib.h>

static PtrHashMap g_pxMapDIR = NULL;

static void map_test(void) {
	int a = 123;
	int b = 456;
	int c = 789;

	void *key = &a;
	void *value = &b;

	/* 插入 */
	map_put(&g_pxMapDIR, key, value);

	/* 查询 */
	int *found = map_get(g_pxMapDIR, key);
	if (found != NULL) {
		log_msg("found: %d\n", *found);
	} else {
		log_msg("not found\n");
	}

	/* 更新同一个 key */
	map_put(&g_pxMapDIR, key, &c);

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

int main(void)
{
	sqfs_mgr_load_file();
	map_test();
	
	return 0;
}
