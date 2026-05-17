#include "uthash.h"
#include "logging.h"

struct PtrHashMapEntry {
    void *key;
    void *value;
    UT_hash_handle hh;
};

typedef struct PtrHashMapEntry * PtrHashMap;

static struct PtrHashMapEntry *g_pxMapDIR = NULL;

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

void map_test(void) {
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