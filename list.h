#ifndef __AM_LIST_H__
#define __AM_LIST_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"

/* ===== 线性表 ===== */
struct am_list_t {
    am_object_t **data;
    uint64_t      size;      // 已使用范围（data[0..size-1] 已初始化，可能含 NULL）
    uint64_t      count;     // 有效（非 NULL）元素数
    uint64_t      capacity;  // 总容量
};

/* ===== 遍历回调类型 ===== */
typedef void (*am_list_iter_cb_t)(uint64_t index, am_object_t *value, void *user_data);

/* ===== 内部：确保容量 ===== */
static inline int am_list_ensure_capacity(am_list_t *list, uint64_t min_cap) {
    if (min_cap <= list->capacity) return 0;
    uint64_t new_cap = list->capacity ? list->capacity : 16;
    while (new_cap < min_cap) new_cap <<= 1;
    am_object_t **new_data = (am_object_t**)realloc(list->data, new_cap * sizeof(am_object_t*));
    if (!new_data) return -1;
    list->data = new_data;
    list->capacity = new_cap;
    return 0;
}

/* ===== 创建线性表 ===== */
static inline am_list_t* am_list_create(uint64_t cap) {
    am_list_t *list = (am_list_t*)malloc(sizeof(am_list_t));
    if (!list) return NULL;
    list->size = 0;
    list->count = 0;
    list->capacity = cap < 16 ? 16 : cap;
    list->data = (am_object_t**)calloc(list->capacity, sizeof(am_object_t*));
    if (!list->data) {
        free(list);
        return NULL;
    }
    return list;
}

/* ===== 清空线性表（保留数据数组）===== */
static inline void am_list_clear(am_list_t *list) {
    if (!list) return;
    memset(list->data, 0, list->capacity * sizeof(am_object_t*));
    list->size = 0;
    list->count = 0;
}

/* ===== 释放线性表 ===== */
static inline void am_list_free(am_list_t *list) {
    if (!list) return;
    free(list->data);
    free(list);
}

/* ===== 按下标访问 ===== */
static inline am_object_t* am_list_get(am_list_t *list, uint64_t index) {
    if (!list || index >= list->size) return NULL;
    return list->data[index];
}

/* ===== 按下标修改（可设为 NULL 作为逻辑删除）===== */
static inline int am_list_set(am_list_t *list, uint64_t index, am_object_t *value) {
    if (!list) return -1;
    if (am_list_ensure_capacity(list, index + 1) != 0) return -1;
    // 将 [size, index) 的间隙初始化为 NULL
    for (uint64_t i = list->size; i < index; i++) {
        list->data[i] = NULL;
    }
    if (index >= list->size) {
        list->size = index + 1;
    }
    // 更新有效元素计数
    am_object_t *old = list->data[index];
    if (old == NULL && value != NULL) {
        list->count++;
    } else if (old != NULL && value == NULL) {
        list->count--;
    }
    list->data[index] = value;
    return 0;
}

/* ===== 尾部追加 ===== */
static inline int am_list_push(am_list_t *list, am_object_t *value) {
    return am_list_set(list, list->size, value);
}

/* ===== 尾部移除 ===== */
static inline am_object_t* am_list_pop(am_list_t *list) {
    if (!list || list->size == 0) return NULL;
    for (int64_t i = (int64_t)list->size - 1; i >= 0; i--) {
        if (list->data[i] != NULL) {
            am_object_t *val = list->data[i];
            list->data[i] = NULL;
            list->count--;
            if ((uint64_t)i == list->size - 1) list->size--;
            return val;
        }
    }
    return NULL;
}

/* ===== 头部插入 ===== */
static inline int am_list_unshift(am_list_t *list, am_object_t *value) {
    if (!list) return -1;
    if (am_list_ensure_capacity(list, list->size + 1) != 0) return -1;
    memmove(list->data + 1, list->data, list->size * sizeof(am_object_t*));
    list->data[0] = value;
    list->size++;
    if (value != NULL) list->count++;
    return 0;
}

/* ===== 头部移除 ===== */
static inline am_object_t* am_list_shift(am_list_t *list) {
    if (!list || list->size == 0) return NULL;
    am_object_t *val = list->data[0];
    if (val != NULL) list->count--;
    memmove(list->data, list->data + 1, (list->size - 1) * sizeof(am_object_t*));
    list->size--;
    return val;
}

/* ===== 获取有效元素个数 ===== */
static inline uint64_t am_list_size(am_list_t *list) {
    return list ? list->count : 0;
}

/* ===== 获取容量 ===== */
static inline uint64_t am_list_capacity(am_list_t *list) {
    return list ? list->capacity : 0;
}

/* ===== 获取物理边界（包含 NULL 槽位）===== */
static inline uint64_t am_list_bound(am_list_t *list) {
    return list ? list->size : 0;
}

/* ===== 遍历（跳过 NULL 槽位）===== */
static inline void am_list_iter(am_list_t *list, am_list_iter_cb_t cb, void *user_data) {
    if (!list || !cb) return;
    for (uint64_t i = 0; i < list->size; i++) {
        if (list->data[i] != NULL) {
            cb(i, list->data[i], user_data);
        }
    }
}

#endif
