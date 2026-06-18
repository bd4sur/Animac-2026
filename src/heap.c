#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "allocator.h"
#include "map.h"
#include "heap.h"

// 全局把柄计数器，保证同一进程内不同 AST 堆的把柄不冲突。
// 后续若需要严格的进程隔离，可将此计数器移入 am_heap_t 并通过模块 ID 哈希生成前缀。
static am_handle_t g_heap_handle_counter = 1;


// ===============================================================================
// 构造函数
// ===============================================================================

am_heap_t *am_heap_create(am_allocator_t *alloc, size_t capacity) {
    if (capacity < 16) capacity = 16;

    am_heap_t *heap = (am_heap_t *)am_malloc(alloc, sizeof(am_heap_t));
    if (!heap) return NULL;

    heap->capacity = capacity;
    heap->table = am_map_create(alloc, capacity);
    heap->metadata = am_map_create(alloc, capacity);
    heap->handle_counter = g_heap_handle_counter;

    if (!heap->table || !heap->metadata) {
        if (heap->table) am_map_destroy(alloc, heap->table);
        if (heap->metadata) am_map_destroy(alloc, heap->metadata);
        am_free(alloc, heap);
        return NULL;
    }

    return heap;
}


// ===============================================================================
// 析构
// ===============================================================================

int32_t am_heap_destroy(am_allocator_t *alloc, am_heap_t *heap) {
    if (!heap) return 0;

    if (heap->table) {
        // 释放所有指针对象
        size_t count = am_map_length(alloc, heap->table);
        am_value_t *keys = am_map_keys(alloc, heap->table);
        for (size_t i = 0; i < count; i++) {
            am_value_t v = am_map_get(alloc, heap->table, keys[i]);
            if (am_value_is_ptr(v)) {
                am_free(alloc, am_value_to_ptr(v));
            }
        }
        free(keys);
        am_map_destroy(alloc, heap->table);
    }

    if (heap->metadata) {
        am_map_destroy(alloc, heap->metadata);
    }

    am_free(alloc, heap);
    return 1;
}


// ===============================================================================
// 拷贝
// ===============================================================================

am_heap_t *am_heap_copy(am_allocator_t *alloc, am_heap_t *heap) {
    if (!heap) return NULL;

    am_heap_t *copy = (am_heap_t *)am_malloc(alloc, sizeof(am_heap_t));
    if (!copy) return NULL;

    copy->capacity = heap->capacity;
    copy->handle_counter = heap->handle_counter;
    copy->table = heap->table ? am_map_copy(alloc, heap->table) : NULL;
    copy->metadata = heap->metadata ? am_map_copy(alloc, heap->metadata) : NULL;

    if ((heap->table && !copy->table) || (heap->metadata && !copy->metadata)) {
        am_heap_destroy(alloc, copy);
        return NULL;
    }

    return copy;
}


// ===============================================================================
// 遍历
// ===============================================================================

typedef struct {
    am_heap_iter_callback_t cb;
    void *user_data;
} am_heap_iter_wrapper_t;

static void am_heap_iter_map_cb(am_value_t key, am_value_t value, void *user_data) {
    am_heap_iter_wrapper_t *ctx = (am_heap_iter_wrapper_t *)user_data;
    am_handle_t handle = am_value_to_handle(key);
    ctx->cb(handle, value, ctx->user_data);
}


void am_heap_iter(am_allocator_t *alloc, am_heap_t *heap, am_heap_iter_callback_t cb, void *user_data) {
    if (!heap || !heap->table || !cb) return;
    am_heap_iter_wrapper_t ctx = { cb, user_data };
    am_map_iter(alloc, heap->table, am_heap_iter_map_cb, &ctx);
}


// ===============================================================================
// 对象二进制转储 TODO
// ===============================================================================

uint8_t *am_heap_dump(am_allocator_t *alloc, am_heap_t *heap, size_t *size) {
    (void)alloc;
    (void)heap;
    if (size) *size = 0;
    return NULL;
}


// ===============================================================================
// 把柄操作
// ===============================================================================

int32_t am_heap_has_handle(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle) {
    if (!heap || !heap->table) return 0;
    return am_map_contains(alloc, heap->table, am_make_value_of_handle(handle));
}


am_handle_t am_heap_alloc_handle(am_allocator_t *alloc, am_heap_t *heap) {
    if (!heap || !heap->table) return AM_HANDLE_NULL;
    if (heap->handle_counter >= heap->capacity) return AM_HANDLE_NULL;

    am_handle_t handle = heap->handle_counter++;
    g_heap_handle_counter = heap->handle_counter;
    am_value_t handle_val = am_make_value_of_handle(handle);

    am_map_t *new_table = am_map_set(alloc, heap->table, handle_val, AM_VALUE_NULL);
    if (!new_table) return AM_HANDLE_NULL;
    heap->table = new_table;

    return handle;
}


int32_t am_heap_free_handle(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle) {
    if (!heap || !heap->table) return 0;

    am_value_t handle_val = am_make_value_of_handle(handle);
    am_value_t v = am_map_get(alloc, heap->table, handle_val);

    if (am_value_is_ptr(v)) {
        am_free(alloc, am_value_to_ptr(v));
    }

    return am_map_delete(alloc, heap->table, handle_val);
}


int32_t am_heap_set_metadata(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle, am_uint_t property) {
    (void)alloc;
    (void)heap;
    (void)handle;
    (void)property;
    return 0;
}


am_uint_t am_heap_get_metadata(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle) {
    (void)alloc;
    (void)heap;
    (void)handle;
    return 0;
}


// ===============================================================================
// 值操作
// ===============================================================================

am_value_t am_heap_get(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle) {
    if (!heap || !heap->table) return AM_VALUE_UNDEFINED;
    return am_map_get(alloc, heap->table, am_make_value_of_handle(handle));
}


int32_t am_heap_set(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle, am_value_t value) {
    if (!heap || !heap->table) return -1;

    am_value_t handle_val = am_make_value_of_handle(handle);
    // 把柄必须遵循先申请后使用的原则，不允许直接创建把柄
    if (!am_map_contains(alloc, heap->table, handle_val)) {
        return -1;
    }

    am_map_t *new_table = am_map_set(alloc, heap->table, handle_val, value);
    if (!new_table) return -1;
    heap->table = new_table;
    return 0;
}
