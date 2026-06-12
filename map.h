#ifndef __AM_MAP_H__
#define __AM_MAP_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////
// 基础数据结构：通用散列表 am_map_t<am_value_t,am_value_t>
///////////////////////////////////////////

// 定义两个特殊key
#define AM_MAP_KEY_EMPTY AM_VALUE_NULL
#define AM_MAP_KEY_TOMBSTONE AM_VALUE_UNDEFINED

// 表项
typedef struct am_map_entry_t {
    am_value_t key;
    am_value_t value;
} am_map_entry_t;

// 散列表（开放寻址法）
typedef struct am_map_t {
    uint32_t size;       // 当前有效键值对数量
    uint32_t capacity;   // 物理槽位数 (必须是2的幂)
    uint32_t mask;       // capacity - 1，用于快速取模
    uint32_t tombstones; // 墓碑数量，用于触发重哈希
    am_map_entry_t slots[];  // 连续槽位区
} am_map_t;

// 遍历回调类型
typedef void (*am_map_iter_callback_t)(am_value_t key, am_value_t value, void *user_data);

// ===============================================================================
// 内部辅助函数
// ===============================================================================

// 计算 am_value_t 的哈希值（基于其底层位模式）
static inline uint32_t am_value_hash(am_value_t v) {
    uint32_t h = (uint32_t)v;
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
    h ^= (uint32_t)(v >> 32);
#endif
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

// am_value_t 键相等性：按位比较
static inline bool am_value_equal(am_value_t a, am_value_t b) {
    return a == b;
}

// 将容量向上取整为不小于它的最小 2 的幂
static inline uint32_t am_map_round_up_capacity(uint32_t capacity) {
    uint32_t cap = 1;
    while (cap < capacity) cap <<= 1;
    return cap;
}

// 查找 key 所在槽位。
// 返回值：1 表示找到，0 表示未找到。
// 无论是否找到，*out_insert_idx 都会返回可插入位置（首个墓碑或空槽）。
static inline int32_t am_map_find_slot(const am_map_t *m, am_value_t key, uint32_t *out_insert_idx) {
    uint32_t idx = am_value_hash(key) & m->mask;
    uint32_t insert_idx = UINT32_MAX;

    while (1) {
        am_value_t k = m->slots[idx].key;
        if (k == AM_MAP_KEY_EMPTY) {
            if (insert_idx == UINT32_MAX) insert_idx = idx;
            *out_insert_idx = insert_idx;
            return 0;
        }
        if (k == AM_MAP_KEY_TOMBSTONE) {
            if (insert_idx == UINT32_MAX) insert_idx = idx;
        } else if (am_value_equal(k, key)) {
            *out_insert_idx = idx;
            return 1;
        }
        idx = (idx + 1) & m->mask;
    }
}

// 原地重哈希：清除墓碑，不改变容量
static inline int32_t am_map_rehash(am_allocator_t *alloc, am_map_t *map);

// 扩容并重哈希到新容量（new_capacity 会被向上取整为 2 的幂）。
// 返回新的 map 对象指针；失败返回 NULL。原 map 对象会被释放，调用者必须使用返回的新指针。
static inline am_map_t *am_map_resize(am_allocator_t *alloc, am_map_t *map, uint32_t new_capacity);

// ===============================================================================
// 构造函数
// ===============================================================================

// 以初始容量新建哈希表。capacity 会被向上取整为不小于它的最小 2 的幂。
// 所有 key 初始化为 AM_MAP_KEY_EMPTY，value 初始化为 AM_VALUE_NULL。
static inline am_map_t *am_map_create(am_allocator_t *alloc, uint32_t capacity) {
    uint32_t cap = am_map_round_up_capacity(capacity);
    if (cap < 8) cap = 8;

    size_t total_size = sizeof(am_map_t) + cap * sizeof(am_map_entry_t);
    am_map_t *map = (am_map_t *)am_malloc(alloc, total_size);
    if (!map) return NULL;

    memset(map, 0, total_size);

    map->capacity = cap;
    map->mask = cap - 1;
    map->size = 0;
    map->tombstones = 0;

    for (uint32_t i = 0; i < cap; i++) {
        map->slots[i].key = AM_MAP_KEY_EMPTY;
        map->slots[i].value = AM_VALUE_NULL;
    }

    return (am_map_t *)map;
}

// ===============================================================================
// 析构与清理
// ===============================================================================

// 清空哈希表：对所有有效 entry，若 value 是指针则先释放，再将 key 置为 EMPTY、value 置为 NULL
static inline int32_t am_map_clear(am_allocator_t *alloc, am_map_t *map) {
    am_map_t *m = (am_map_t *)map;
    for (uint32_t i = 0; i < m->capacity; i++) {
        if (m->slots[i].key != AM_MAP_KEY_EMPTY && m->slots[i].key != AM_MAP_KEY_TOMBSTONE) {
            if (am_value_is_ptr(m->slots[i].value)) {
                am_free(alloc, am_value_to_ptr(m->slots[i].value));
            }
        }
        m->slots[i].key = AM_MAP_KEY_EMPTY;
        m->slots[i].value = AM_VALUE_NULL;
    }
    m->size = 0;
    m->tombstones = 0;
    return 0;
}

// 彻底销毁哈希表对象
static inline int32_t am_map_destroy(am_allocator_t *alloc, am_map_t *map) {
    am_map_clear(alloc, map);
    am_free(alloc, map);
    return 0;
}

// ===============================================================================
// 基本操作
// ===============================================================================

// 查找：返回对应的 value；若不存在返回 AM_VALUE_NULL
static inline am_value_t am_map_get(am_allocator_t *alloc, am_map_t *map, am_value_t key) {
    (void)alloc;
    am_map_t *m = (am_map_t *)map;
    if (m->size == 0) return AM_VALUE_NULL;

    uint32_t idx;
    if (!am_map_find_slot(m, key, &idx)) return AM_VALUE_NULL;
    return m->slots[idx].value;
}

// 存在性检查：存在返回 1，不存在返回 0
static inline int32_t am_map_contains(am_allocator_t *alloc, am_map_t *map, am_value_t key) {
    (void)alloc;
    am_map_t *m = (am_map_t *)map;
    if (m->size == 0) return 0;

    uint32_t idx;
    return am_map_find_slot(m, key, &idx) ? 1 : 0;
}

// 不扩容地插入或修改（stable 版本）。
// 仅做插入/替换，绝不分配或释放 map 对象本身，因此 map 指针保持稳定。
// 若 map 已满且 key 不存在，返回 -1；成功返回 0。
// 替换已存在的 key 时，会释放旧的指针 value。
static inline int32_t am_map_set_stable(am_allocator_t *alloc, am_map_t *map, am_value_t key, am_value_t value) {
    if (key == AM_MAP_KEY_EMPTY || key == AM_MAP_KEY_TOMBSTONE) return -1;

    am_map_t *m = (am_map_t *)map;

    // 表已完全填满（无空槽、无墓碑）：只能替换已有 key。
    if (m->size == m->capacity) {
        uint32_t idx = am_value_hash(key) & m->mask;
        for (uint32_t i = 0; i < m->capacity; i++) {
            am_value_t k = m->slots[idx].key;
            if (am_value_equal(k, key)) {
                if (am_value_is_ptr(m->slots[idx].value)) {
                    am_free(alloc, am_value_to_ptr(m->slots[idx].value));
                }
                m->slots[idx].value = value;
                return 0;
            }
            idx = (idx + 1) & m->mask;
        }
        return -1; // 表满且 key 不存在
    }

    // 存在空槽或墓碑，find_slot 必然终止
    uint32_t idx;
    int32_t found = am_map_find_slot(m, key, &idx);
    if (found) {
        if (am_value_is_ptr(m->slots[idx].value)) {
            am_free(alloc, am_value_to_ptr(m->slots[idx].value));
        }
        m->slots[idx].value = value;
    } else {
        if (m->slots[idx].key == AM_MAP_KEY_TOMBSTONE) {
            m->tombstones--;
        }
        m->slots[idx].key = key;
        m->slots[idx].value = value;
        m->size++;
    }
    return 0;
}

// 插入或修改。
// 插入新键值对；若 key 已存在则替换 value，并释放旧的指针 value。
// 当负载因子（含墓碑）超过 75% 时自动扩容。
// 返回新的 map 对象指针；失败返回 NULL。调用者必须使用返回的指针替换原有 map 指针。
static inline am_map_t *am_map_set(am_allocator_t *alloc, am_map_t *map, am_value_t key, am_value_t value) {
    if (key == AM_MAP_KEY_EMPTY || key == AM_MAP_KEY_TOMBSTONE) return NULL;

    am_map_t *m = (am_map_t *)map;

    // 负载因子超过 75% 时扩容
    if ((m->size + m->tombstones + 1) * 4 > m->capacity * 3) {
        am_map_t *new_map = am_map_resize(alloc, map, m->capacity * 2);
        if (!new_map) return NULL;
        map = new_map;
        m = (am_map_t *)map;
    }

    uint32_t idx;
    int32_t found = am_map_find_slot(m, key, &idx);
    if (found) {
        if (am_value_is_ptr(m->slots[idx].value)) {
            am_free(alloc, am_value_to_ptr(m->slots[idx].value));
        }
        m->slots[idx].value = value;
    } else {
        if (m->slots[idx].key == AM_MAP_KEY_TOMBSTONE) {
            m->tombstones--;
        }
        m->slots[idx].key = key;
        m->slots[idx].value = value;
        m->size++;
    }
    return map;
}

// 删除指定 key。若存在且 value 为指针则释放。
// 返回 1 表示删除成功，0 表示 key 不存在。
static inline int32_t am_map_delete(am_allocator_t *alloc, am_map_t *map, am_value_t key) {
    am_map_t *m = (am_map_t *)map;
    if (m->size == 0) return 0;

    uint32_t idx;
    if (!am_map_find_slot(m, key, &idx)) return 0;

    if (am_value_is_ptr(m->slots[idx].value)) {
        am_free(alloc, am_value_to_ptr(m->slots[idx].value));
    }
    m->slots[idx].key = AM_MAP_KEY_TOMBSTONE;
    m->slots[idx].value = AM_VALUE_NULL;
    m->size--;
    m->tombstones++;

    // 墓碑过多时原地重哈希（失败则 map 未被修改，继续保留墓碑）
    if (m->tombstones * 2 > m->capacity) {
        if (am_map_rehash(alloc, map) != 0) {
            // 内存不足，重哈希失败；删除操作本身已完成
        }
    }
    return 1;
}

// 当前有效键值对数量
static inline uint32_t am_map_size(am_allocator_t *alloc, am_map_t *map) {
    (void)alloc;
    return ((am_map_t *)map)->size;
}

// 物理槽位数
static inline uint32_t am_map_capacity(am_allocator_t *alloc, am_map_t *map) {
    (void)alloc;
    return ((am_map_t *)map)->capacity;
}

// ===============================================================================
// 遍历与键列表
// ===============================================================================

// 遍历所有有效键值对，调用回调 cb
static inline void am_map_iter(am_allocator_t *alloc, am_map_t *map, am_map_iter_callback_t cb, void *user_data) {
    (void)alloc;
    if (!cb) return;
    am_map_t *m = (am_map_t *)map;
    for (uint32_t i = 0; i < m->capacity; i++) {
        if (m->slots[i].key != AM_MAP_KEY_EMPTY && m->slots[i].key != AM_MAP_KEY_TOMBSTONE) {
            cb(m->slots[i].key, m->slots[i].value, user_data);
        }
    }
}

// 获取所有 key 的副本列表，使用系统 malloc 分配。
// 调用者负责使用 free() 释放返回的指针；size 为 0 时返回 NULL。
static inline am_value_t *am_map_keys(am_allocator_t *alloc, am_map_t *map) {
    (void)alloc;
    am_map_t *m = (am_map_t *)map;
    if (m->size == 0) return NULL;

    am_value_t *keys = (am_value_t *)malloc(m->size * sizeof(am_value_t));
    if (!keys) return NULL;

    uint32_t count = 0;
    for (uint32_t i = 0; i < m->capacity; i++) {
        if (m->slots[i].key != AM_MAP_KEY_EMPTY && m->slots[i].key != AM_MAP_KEY_TOMBSTONE) {
            keys[count++] = m->slots[i].key;
        }
    }
    return keys;
}

// ===============================================================================
// 重哈希与扩容实现
// ===============================================================================

static inline int32_t am_map_rehash(am_allocator_t *alloc, am_map_t *map) {
    am_map_t *m = (am_map_t *)map;
    uint32_t cap = m->capacity;
    size_t entries_size = cap * sizeof(am_map_entry_t);

    am_map_entry_t *old_slots = (am_map_entry_t *)am_malloc(alloc, entries_size);
    if (!old_slots) return -1;
    memcpy(old_slots, m->slots, entries_size);

    for (uint32_t i = 0; i < cap; i++) {
        m->slots[i].key = AM_MAP_KEY_EMPTY;
        m->slots[i].value = AM_VALUE_NULL;
    }
    m->size = 0;
    m->tombstones = 0;

    for (uint32_t i = 0; i < cap; i++) {
        if (old_slots[i].key != AM_MAP_KEY_EMPTY && old_slots[i].key != AM_MAP_KEY_TOMBSTONE) {
            uint32_t insert_idx;
            am_map_find_slot(m, old_slots[i].key, &insert_idx);
            m->slots[insert_idx].key = old_slots[i].key;
            m->slots[insert_idx].value = old_slots[i].value;
            m->size++;
        }
    }

    am_free(alloc, old_slots);
    return 0;
}

static inline am_map_t *am_map_resize(am_allocator_t *alloc, am_map_t *map, uint32_t new_capacity) {
    am_map_t *m = (am_map_t *)map;
    uint32_t cap = am_map_round_up_capacity(new_capacity);
    if (cap <= m->capacity) {
        // 容量未增加：仅做重哈希清理墓碑
        if (am_map_rehash(alloc, map) != 0) return NULL;
        return map;
    }

    uint32_t old_capacity = m->capacity;
    size_t old_entries_size = old_capacity * sizeof(am_map_entry_t);

    am_map_entry_t *old_slots = NULL;
    if (old_entries_size > 0) {
        old_slots = (am_map_entry_t *)am_malloc(alloc, old_entries_size);
        if (!old_slots) return NULL;
        memcpy(old_slots, m->slots, old_entries_size);
    }

    size_t new_total_size = sizeof(am_map_t) + cap * sizeof(am_map_entry_t);
    am_map_t *new_m = (am_map_t *)am_malloc(alloc, new_total_size);
    if (!new_m) {
        am_free(alloc, old_slots);
        return NULL;
    }

    new_m->capacity = cap;
    new_m->mask = cap - 1;
    new_m->size = 0;
    new_m->tombstones = 0;

    for (uint32_t i = 0; i < cap; i++) {
        new_m->slots[i].key = AM_MAP_KEY_EMPTY;
        new_m->slots[i].value = AM_VALUE_NULL;
    }

    for (uint32_t i = 0; i < old_capacity; i++) {
        if (old_slots[i].key != AM_MAP_KEY_EMPTY && old_slots[i].key != AM_MAP_KEY_TOMBSTONE) {
            uint32_t insert_idx;
            am_map_find_slot(new_m, old_slots[i].key, &insert_idx);
            new_m->slots[insert_idx].key = old_slots[i].key;
            new_m->slots[insert_idx].value = old_slots[i].value;
            new_m->size++;
        }
    }

    am_free(alloc, old_slots);
    am_free(alloc, m);
    return (am_map_t *)new_m;
}

#ifdef __cplusplus
}
#endif

#endif
