#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "allocator.h"
#include "map.h"
#include "list.h"
#include "wstring.h"
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
    return 0;
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
// 对象二进制转储
// ===============================================================================

// 辅助：对heap的entry按handle升序排序（用于deep_dump时保证顺序稳定）。
typedef struct {
    am_value_t key;
    am_value_t value;
    am_map_entry_t *slot;
} am_heap_entry_t;

static int am_heap_entry_compare(const void *a, const void *b) {
    const am_heap_entry_t *ea = (const am_heap_entry_t *)a;
    const am_heap_entry_t *eb = (const am_heap_entry_t *)b;
    am_handle_t ha = am_value_to_handle(ea->key);
    am_handle_t hb = am_value_to_handle(eb->key);
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
}

// 功能说明：将am_heap_t对象序列化成二进制序列，并转储到buffer[offset]
// 实现说明：offset是写入buffer的起点offset。成功则返回向buffer新增字节数，失败则返回SIZE_MAX。
// 注意：若buffer设为NULL，或者offset设为SIZE_MAX，则仅计算转储后的二进制序列的字节数，不实际写入buffer。
//       压缩底层map对象，将table和metadata的capacity压缩到跟length一致，删除多余分配的空闲部分。
size_t am_heap_dump(am_allocator_t *alloc, am_heap_t *heap, uint8_t *buffer, size_t offset) {
    if (!heap) return SIZE_MAX;

    size_t table_dump_size = heap->table ? am_map_dump(alloc, heap->table, NULL, 0) : 0;
    size_t metadata_dump_size = heap->metadata ? am_map_dump(alloc, heap->metadata, NULL, 0) : 0;

    if ((heap->table && table_dump_size == SIZE_MAX) ||
        (heap->metadata && metadata_dump_size == SIZE_MAX)) {
        return SIZE_MAX;
    }

    size_t heap_dump_size = sizeof(am_heap_t) + table_dump_size + metadata_dump_size;

    if (buffer != NULL && offset != SIZE_MAX) {
        am_heap_t *dump = (am_heap_t *)&buffer[offset];
        dump->capacity = heap->capacity;
        dump->handle_counter = heap->handle_counter;
        // table/metadata偏移量以heap dump起点为基准，便于深dump整体自描述
        dump->table = heap->table ? (am_map_t *)sizeof(am_heap_t) : NULL;
        dump->metadata = heap->metadata ? (am_map_t *)(sizeof(am_heap_t) + table_dump_size) : NULL;

        if (heap->table) {
            size_t written = am_map_dump(alloc, heap->table, buffer, offset + sizeof(am_heap_t));
            if (written != table_dump_size) return SIZE_MAX;
        }
        if (heap->metadata) {
            size_t written = am_map_dump(alloc, heap->metadata, buffer, offset + sizeof(am_heap_t) + table_dump_size);
            if (written != metadata_dump_size) return SIZE_MAX;
        }
    }

    return heap_dump_size;
}


// 功能说明：am_heap_dump的逆操作。从二进制字节序列buffer[offset]开始，读取转储的heap对象，构造heap并返回其指针。
// 实现说明：成功则返回加载后am_heap_t对象的指针，失败则返回NULL。
am_heap_t *am_heap_load(am_allocator_t *alloc, uint8_t *buffer, size_t offset) {
    if (!alloc || !buffer) return NULL;

    am_heap_t *dump = (am_heap_t *)&buffer[offset];
    if (!dump->table) return NULL;

    am_heap_t *heap = (am_heap_t *)am_malloc(alloc, sizeof(am_heap_t));
    if (!heap) return NULL;

    heap->capacity = dump->capacity;
    heap->handle_counter = dump->handle_counter;

    // table/metadata偏移量以heap dump起点为基准
    size_t table_offset = (size_t)dump->table;
    size_t metadata_offset = (size_t)dump->metadata;

    heap->table = am_map_load(alloc, buffer, offset + table_offset);
    if (!heap->table) {
        am_free(alloc, heap);
        return NULL;
    }

    if (metadata_offset != 0) {
        heap->metadata = am_map_load(alloc, buffer, offset + metadata_offset);
        if (!heap->metadata) {
            am_map_destroy(alloc, heap->table);
            am_free(alloc, heap);
            return NULL;
        }
    } else {
        heap->metadata = NULL;
    }

    return heap;
}


// 功能说明：深度转储整个heap及其指向的对象
// 实现说明：offset是写入buffer的起点offset。成功则返回向buffer新增字节数，失败则返回SIZE_MAX。
// 注意：若buffer设为NULL，或者offset设为SIZE_MAX，则仅计算转储后的二进制序列的字节数，不实际写入buffer。
//       仅处理value为ptr且指向AM_OBJECT_TYPE_LIST或AM_OBJECT_TYPE_WSTRING类型对象的情况。
size_t am_heap_deep_dump(am_allocator_t *alloc, am_heap_t *heap, uint8_t *buffer, size_t offset) {
    if (!alloc || !heap || !heap->table) return SIZE_MAX;

    // 先计算heap对象本身的dump大小（不实际写入，因为后续还要修改table中的value）
    size_t heap_map_size = am_heap_dump(alloc, heap, NULL, 0);
    if (heap_map_size == SIZE_MAX) return SIZE_MAX;

    size_t buffer_offset = offset + 16; // 留出两个uint64_t长度字段
    size_t obj_offset = buffer_offset + heap_map_size;

    size_t count = am_map_length(alloc, heap->table);
    am_heap_entry_t *entries = NULL;
    if (count > 0) {
        entries = (am_heap_entry_t *)malloc(count * sizeof(am_heap_entry_t));
        if (!entries) return SIZE_MAX;

        size_t idx = 0;
        for (size_t i = 0; i < heap->table->capacity; i++) {
            am_value_t k = heap->table->slots[i].key;
            if (k != AM_MAP_KEY_EMPTY && k != AM_MAP_KEY_TOMBSTONE) {
                entries[idx].key = k;
                entries[idx].value = heap->table->slots[i].value;
                entries[idx].slot = &heap->table->slots[i];
                idx++;
            }
        }

        if (idx != count) {
            free(entries);
            return SIZE_MAX;
        }

        qsort(entries, count, sizeof(am_heap_entry_t), am_heap_entry_compare);
    }

    for (size_t i = 0; i < count; i++) {
        am_value_t value = entries[i].value;

        if (!am_value_is_ptr(value)) {
            free(entries);
            return SIZE_MAX;
        }

        am_object_t *obj = am_value_to_ptr(value);
        size_t obj_size = SIZE_MAX;

        switch (obj->type) {
            case AM_OBJECT_TYPE_LIST:
                obj_size = am_list_dump(alloc, (am_list_t *)obj, buffer, obj_offset);
                break;
            case AM_OBJECT_TYPE_WSTRING:
                obj_size = am_wstring_dump(alloc, (am_wstring_t *)obj, buffer, obj_offset);
                break;
            default:
                free(entries);
                return SIZE_MAX;
        }

        if (obj_size == SIZE_MAX) {
            free(entries);
            return SIZE_MAX;
        }

        if (buffer != NULL && offset != SIZE_MAX) {
            // 对象偏移量以deep_dump区域起点为基准，便于整体自描述与重定位
            am_value_t offset_value = am_make_value_of_ptr((am_object_t *)(uintptr_t)(obj_offset - offset));
            entries[i].slot->value = offset_value;
        }

        obj_offset += obj_size;
    }

    if (entries) free(entries);

    // 将修改后的heap对象dump到buffer[offset+16]
    size_t written = am_heap_dump(alloc, heap, buffer, buffer_offset);
    if (written != heap_map_size) return SIZE_MAX;

    // 写入总字节长度和heap dump长度
    if (buffer != NULL && offset != SIZE_MAX) {
        uint64_t total_size = (uint64_t)(obj_offset - offset);
        uint64_t heap_size_u64 = (uint64_t)heap_map_size;
        memcpy(&buffer[offset], &total_size, sizeof(uint64_t));
        memcpy(&buffer[offset + 8], &heap_size_u64, sizeof(uint64_t));
    }

    return obj_offset - offset;
}


// 功能说明：am_heap_deep_dump的逆操作。从二进制字节序列buffer[offset]开始，读取转储的heap及其指向的对象，构造heap并返回其指针。
// 实现说明：成功则返回加载后am_heap_t对象的指针，失败则返回NULL。
// 注意：仅处理value为ptr且指向AM_OBJECT_TYPE_LIST或AM_OBJECT_TYPE_WSTRING类型对象的情况。
am_heap_t *am_heap_deep_load(am_allocator_t *alloc, uint8_t *buffer, size_t offset) {
    if (!alloc || !buffer) return NULL;

    uint64_t total_size, heap_size;
    memcpy(&total_size, &buffer[offset], sizeof(uint64_t));
    memcpy(&heap_size, &buffer[offset + 8], sizeof(uint64_t));
    (void)total_size;

    am_heap_t *heap = am_heap_load(alloc, buffer, offset + 16);
    if (!heap) return NULL;

    size_t count = am_map_length(alloc, heap->table);
    am_object_t **loaded = NULL;
    if (count > 0) {
        loaded = (am_object_t **)calloc(count, sizeof(am_object_t *));
        if (!loaded) {
            am_heap_destroy(alloc, heap);
            return NULL;
        }
    }

    size_t idx = 0;
    for (size_t i = 0; i < heap->table->capacity; i++) {
        am_value_t k = heap->table->slots[i].key;
        if (k == AM_MAP_KEY_EMPTY || k == AM_MAP_KEY_TOMBSTONE) continue;

        am_value_t v = heap->table->slots[i].value;
        if (!am_value_is_ptr(v)) continue;

        size_t obj_rel_offset = (size_t)am_value_to_ptr(v);
        am_object_t *obj_header = (am_object_t *)&buffer[offset + obj_rel_offset];
        am_object_t *obj = NULL;

        if (obj_header->type == AM_OBJECT_TYPE_LIST) {
            obj = (am_object_t *)am_list_load(alloc, buffer, offset + obj_rel_offset);
        } else if (obj_header->type == AM_OBJECT_TYPE_WSTRING) {
            obj = (am_object_t *)am_wstring_load(alloc, buffer, offset + obj_rel_offset);
        } else {
            // 不支持的类型：清理已加载对象
            for (size_t j = 0; j < idx; j++) {
                am_free(alloc, loaded[j]);
            }
            free(loaded);
            // 将table中的偏移量值清空，避免am_heap_destroy误释放
            for (size_t j = 0; j < heap->table->capacity; j++) {
                heap->table->slots[j].value = AM_VALUE_NULL;
            }
            am_heap_destroy(alloc, heap);
            return NULL;
        }

        if (!obj) {
            for (size_t j = 0; j < idx; j++) {
                am_free(alloc, loaded[j]);
            }
            free(loaded);
            for (size_t j = 0; j < heap->table->capacity; j++) {
                heap->table->slots[j].value = AM_VALUE_NULL;
            }
            am_heap_destroy(alloc, heap);
            return NULL;
        }

        heap->table->slots[i].value = am_make_value_of_ptr(obj);
        loaded[idx++] = obj;
    }

    free(loaded);
    return heap;
}


// ===============================================================================
// 把柄操作
// ===============================================================================

int32_t am_heap_has_handle(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle) {
    if (!heap || !heap->table) return -1;
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
    if (!heap || !heap->table) return -1;

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
    if (am_map_contains(alloc, heap->table, handle_val) < 0) {
        return -1;
    }

    am_map_t *new_table = am_map_set(alloc, heap->table, handle_val, value);
    if (!new_table) return -1;
    heap->table = new_table;
    return 0;
}
