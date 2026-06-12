#ifndef __AM_ALLOCATOR_H__
#define __AM_ALLOCATOR_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

///////////////////////////////////////////
// 抽象内存分配器
///////////////////////////////////////////

// 定义抽象内存管理的虚接口（虚函数表）
typedef struct am_allocator_vtable_t {
    void* (*malloc)(void *state, size_t size);
    void* (*calloc)(void *state, size_t size);
    void* (*realloc)(void *state, void *ptr, size_t size);
    void  (*free)(void *state, void *ptr);
    void  (*destroy)(void *state); // 销毁整个分配器
} am_allocator_vtable_t;

// 抽象内存管理器：其实现待定
typedef struct am_allocator_t {
    const am_allocator_vtable_t *vtable; // 指向具体的实现
    void *state; // TODO 具体策略的上下文（如FreeList的头指针，Arena的指针等）
} am_allocator_t;

// 抽象内存管理接口
static inline void* am_malloc(am_allocator_t *alloc, size_t size) {
    return alloc->vtable->malloc(alloc->state, size);
}
static inline void* am_calloc(am_allocator_t *alloc, size_t size) {
    return alloc->vtable->calloc(alloc->state, size);
}
static inline void* am_realloc(am_allocator_t *alloc, void *ptr, size_t size) {
    return alloc->vtable->realloc(alloc->state, ptr, size);
}
static inline void am_free(am_allocator_t *alloc, void *ptr) {
    return alloc->vtable->free(alloc->state, ptr);
}

// 示例：虚函数的具体实现
// void* am_malloc_impl(void *state, size_t size) { return malloc(size); }
// void am_free_impl(void *state, void *ptr) { free(ptr); }
// const am_allocator_vtable_t malloc_vtable = { am_malloc_impl, am_free_impl, NULL };


#endif