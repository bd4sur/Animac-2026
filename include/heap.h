#ifndef __AM_HEAP_H__
#define __AM_HEAP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "map.h"
#include "allocator.h"

///////////////////////////////////////////
// 解释器基础设施：抽象堆
//   抽象堆的实质是 am_handle_t 到 am_value_t 的映射表，以及管理用元数据。
//   抽象堆的功能是 管理把柄（逻辑地址），将逻辑地址与物理地址解耦，使得无论物理地址怎么变化，把柄永远指向同一个对象。
//   抽象堆的归属是 每个进程拥有属于自己的堆实例，以此实现逻辑地址的进程隔离。
//   抽象堆的基础是 RT提供的内存分配器，堆上存储的value的指针指向的都是RT（宿主环境）提供的物理内存。
///////////////////////////////////////////

// 堆数据结构
typedef struct am_heap_t {
    size_t   capacity; // 初始化时即固定大小，不可扩容
    am_map_t *table;
    am_map_t *metadata;
    am_handle_t handle_counter; // 简单的自增计数器
} am_heap_t;

// 遍历回调类型
typedef void (*am_heap_iter_callback_t)(am_handle_t handle, am_value_t value, void *user_data);


am_heap_t *am_heap_create(am_allocator_t *alloc, size_t capacity);

int32_t am_heap_destroy(am_allocator_t *alloc, am_heap_t *heap);

am_heap_t *am_heap_copy(am_allocator_t *alloc, am_heap_t *heap);

void am_heap_iter(am_allocator_t *alloc, am_heap_t *heap, am_heap_iter_callback_t cb, void *user_data);

// 将对象的二进制内存布局从alloc管理的内存中倒出来，返回一个系统malloc的二进制序列，以及序列长度
//   注意：压缩对象，将capacity压缩到跟length一致，删除多余分配的空闲部分
uint8_t *am_heap_dump(am_allocator_t *alloc, am_heap_t *heap, size_t *size);



// 存在性检查：存在返回 0，不存在或 heap/table 为空返回 -1。
int32_t am_heap_has_handle(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle);

am_handle_t am_heap_alloc_handle(am_allocator_t *alloc, am_heap_t *heap);

// 不仅删除entry，还要穿透free对应的堆对象（被GC调用）。
// 释放成功返回 0；handle 不存在或 heap/table 为空返回 -1。
int32_t am_heap_free_handle(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle);

int32_t am_heap_set_metadata(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle, am_uint_t property); // TODO

am_uint_t am_heap_get_metadata(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle); // TODO


am_value_t am_heap_get(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle);

int32_t am_heap_set(am_allocator_t *alloc, am_heap_t *heap, am_handle_t handle, am_value_t value); // 不扩容，且set前检查把柄有效性





#ifdef __cplusplus
}
#endif

#endif
