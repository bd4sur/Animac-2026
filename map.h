#ifndef __AM_MAP_H__
#define __AM_MAP_H__

#include <stdint.h>
#include <stdlib.h>
#include "object.h"

/* ===== 哈希表节点 ===== */
typedef struct am_map_node_t {
    am_handle_t         key;
    am_object_t        *value;
    struct am_map_node_t *next;
} am_map_node_t;

/* ===== 哈希表 ===== */
struct am_map_t {
    am_map_node_t **buckets;
    uint64_t        capacity;
    uint64_t        size;
};

/* ===== 遍历回调类型 ===== */
typedef void (*am_map_iter_cb_t)(am_handle_t key, am_object_t *value, void *user_data);

/* ===== 辅助：计算下一个 2 的幂次方 ===== */
static inline uint64_t am_map_next_pow2(uint64_t n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    n++;
    return n ? n : 1;
}

/* ===== 哈希函数 ===== */
static inline uint64_t am_map_hash(am_handle_t key, uint64_t cap) {
    // 基于 Fibonacci hashing 的扰动，要求 cap 为 2 的幂次方
    return (key * 11400714819323198485ULL) >> (__builtin_clzll(cap) ^ 63);
}

/* ===== 创建哈希表 ===== */
static inline am_map_t* am_map_create(uint64_t cap) {
    am_map_t *map = (am_map_t*)malloc(sizeof(am_map_t));
    if (!map) return NULL;
    map->capacity = am_map_next_pow2(cap < 16 ? 16 : cap);
    map->size = 0;
    map->buckets = (am_map_node_t**)calloc(map->capacity, sizeof(am_map_node_t*));
    if (!map->buckets) {
        free(map);
        return NULL;
    }
    return map;
}

/* ===== 清空哈希表（保留桶数组）===== */
static inline void am_map_clear(am_map_t *map) {
    if (!map) return;
    for (uint64_t i = 0; i < map->capacity; i++) {
        am_map_node_t *node = map->buckets[i];
        while (node) {
            am_map_node_t *next = node->next;
            free(node);
            node = next;
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
}

/* ===== 释放哈希表 ===== */
static inline void am_map_free(am_map_t *map) {
    if (!map) return;
    am_map_clear(map);
    free(map->buckets);
    free(map);
}

/* ===== 查找 ===== */
static inline am_object_t* am_map_get(am_map_t *map, am_handle_t key) {
    if (!map) return NULL;
    uint64_t idx = am_map_hash(key, map->capacity);
    am_map_node_t *node = map->buckets[idx];
    while (node) {
        if (node->key == key) return node->value;
        node = node->next;
    }
    return NULL;
}

/* ===== 存在性检查 ===== */
static inline int am_map_contains(am_map_t *map, am_handle_t key) {
    return am_map_get(map, key) != NULL;
}

/* ===== 插入 / 更新 ===== */
static inline int am_map_set(am_map_t *map, am_handle_t key, am_object_t *value) {
    if (!map) return -1;
    uint64_t idx = am_map_hash(key, map->capacity);
    am_map_node_t *node = map->buckets[idx];
    while (node) {
        if (node->key == key) {
            node->value = value;  // 更新已有键
            return 0;
        }
        node = node->next;
    }
    // 新建节点，头插法
    node = (am_map_node_t*)malloc(sizeof(am_map_node_t));
    if (!node) return -1;
    node->key   = key;
    node->value = value;
    node->next  = map->buckets[idx];
    map->buckets[idx] = node;
    map->size++;
    return 0;
}

/* ===== 删除 ===== */
static inline int am_map_remove(am_map_t *map, am_handle_t key) {
    if (!map) return -1;
    uint64_t idx = am_map_hash(key, map->capacity);
    am_map_node_t *node = map->buckets[idx];
    am_map_node_t *prev = NULL;
    while (node) {
        if (node->key == key) {
            if (prev) prev->next = node->next;
            else      map->buckets[idx] = node->next;
            free(node);
            map->size--;
            return 0;
        }
        prev = node;
        node = node->next;
    }
    return -1;  // 未找到
}

/* ===== 获取元素个数 ===== */
static inline uint64_t am_map_size(am_map_t *map) {
    return map ? map->size : 0;
}

/* ===== 获取桶数量（容量）===== */
static inline uint64_t am_map_capacity(am_map_t *map) {
    return map ? map->capacity : 0;
}

/* ===== 遍历 ===== */
static inline void am_map_iter(am_map_t *map, am_map_iter_cb_t cb, void *user_data) {
    if (!map || !cb) return;
    for (uint64_t i = 0; i < map->capacity; i++) {
        am_map_node_t *node = map->buckets[i];
        while (node) {
            cb(node->key, node->value, user_data);
            node = node->next;
        }
    }
}

/* ===== 获取所有 key ===== */
static inline am_handle_t* am_map_keys(am_map_t *map, uint64_t *out_count) {
    if (!map) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    if (map->size == 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    am_handle_t *keys = (am_handle_t*)malloc(map->size * sizeof(am_handle_t));
    if (!keys) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    uint64_t idx = 0;
    for (uint64_t i = 0; i < map->capacity; i++) {
        am_map_node_t *node = map->buckets[i];
        while (node) {
            keys[idx++] = node->key;
            node = node->next;
        }
    }
    if (out_count) *out_count = map->size;
    return keys;
}

#endif
