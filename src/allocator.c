#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "allocator.h"
#include "object.h"
#include "map.h"
#include "heap.h"

#define AM_ALLOC_ALIGN      (sizeof(void *))
#define AM_ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))

/* =============================================================================
 * VM 工作区分配器：bump pointer 策略
 * 工作区不进行细粒度回收，仅在关键时间点整体重置。
 * ============================================================================ */

typedef struct am_bump_header_t {
    size_t size; /* 包含本头部的总字节数，已对齐 */
} am_bump_header_t;

typedef struct am_bump_state_t {
    uint8_t *base;
    uint8_t *top;
    uint8_t *end;
} am_bump_state_t;

static void *bump_malloc(void *state, size_t size) {
    am_bump_state_t *s = (am_bump_state_t *)state;
    if (size == 0 || !s) return NULL;

    size_t total = AM_ALIGN_UP(sizeof(am_bump_header_t) + size, AM_ALLOC_ALIGN);
    if (s->top + total > s->end) {
        return NULL;
    }

    am_bump_header_t *h = (am_bump_header_t *)s->top;
    h->size = total;
    s->top += total;
    return (uint8_t *)h + sizeof(am_bump_header_t);
}

static void *bump_calloc(void *state, size_t size) {
    void *p = bump_malloc(state, size);
    if (p) memset(p, 0, size);
    return p;
}

static void *bump_realloc(void *state, void *ptr, size_t size) {
    if (ptr == NULL) return bump_malloc(state, size);
    if (size == 0) {
        /* bump 分配器不回收单个对象 */
        return NULL;
    }

    am_bump_header_t *h = (am_bump_header_t *)((uint8_t *)ptr - sizeof(am_bump_header_t));
    size_t old_payload = h->size - sizeof(am_bump_header_t);
    if (size <= old_payload) return ptr;

    void *new_ptr = bump_malloc(state, size);
    if (new_ptr) {
        size_t copy = old_payload < size ? old_payload : size;
        memcpy(new_ptr, ptr, copy);
    }
    /* 原内存不释放，等待整体重置 */
    return new_ptr;
}

static void bump_free(void *state, void *ptr) {
    (void)state;
    (void)ptr;
    /* bump 分配器不支持单个释放 */
}

static void bump_destroy(void *state) {
    (void)state;
}

static const am_allocator_vtable_t bump_vtable = {
    bump_malloc,
    bump_calloc,
    bump_realloc,
    bump_free,
    bump_destroy
};

/* =============================================================================
 * 堆区分配器：First-Fit Free-List + 边界标签合并
 * 这是受 GC 管理的用户堆区的底层物理内存分配器。
 * ============================================================================ */

typedef struct am_heap_block_header_t {
    size_t size;       /* 总块大小（含头部），最低位为 1 表示已分配 */
    size_t prev_size;  /* 前一个块的总大小，首块为 0 */
    struct am_heap_block_header_t *next_free;
    struct am_heap_block_header_t *prev_free;
    bool live;         /* 压缩阶段使用的临时标记 */
} am_heap_block_header_t;

#define AM_HEAP_HEADER_SIZE AM_ALIGN_UP(sizeof(am_heap_block_header_t), AM_ALLOC_ALIGN)
#define AM_BLOCK_USED_FLAG  ((size_t)1)
#define AM_BLOCK_MIN_SIZE   (AM_HEAP_HEADER_SIZE + AM_ALLOC_ALIGN)

typedef struct am_freelist_state_t {
    uint8_t *base;
    size_t capacity;
    am_heap_block_header_t *free_list_head;
    size_t used_bytes;
} am_freelist_state_t;

static inline size_t block_real_size(const am_heap_block_header_t *b) {
    return b->size & ~AM_BLOCK_USED_FLAG;
}

static inline bool block_is_used(const am_heap_block_header_t *b) {
    return (b->size & AM_BLOCK_USED_FLAG) != 0;
}

static inline void block_set_size(am_heap_block_header_t *b, size_t sz, bool used) {
    b->size = (sz & ~AM_BLOCK_USED_FLAG) | (used ? AM_BLOCK_USED_FLAG : 0);
}

static inline uint8_t *block_payload(const am_heap_block_header_t *b) {
    return (uint8_t *)b + AM_HEAP_HEADER_SIZE;
}

static inline am_heap_block_header_t *block_from_payload(void *p) {
    return (am_heap_block_header_t *)((uint8_t *)p - AM_HEAP_HEADER_SIZE);
}

static inline am_heap_block_header_t *block_next(const am_freelist_state_t *s,
                                                  const am_heap_block_header_t *b) {
    uint8_t *p = (uint8_t *)b + block_real_size(b);
    if (p >= s->base + s->capacity) return NULL;
    return (am_heap_block_header_t *)p;
}

static inline am_heap_block_header_t *block_prev(const am_freelist_state_t *s,
                                                  const am_heap_block_header_t *b) {
    size_t ps = b->prev_size & ~AM_BLOCK_USED_FLAG;
    (void)s;
    if (ps == 0) return NULL;
    return (am_heap_block_header_t *)((uint8_t *)b - ps);
}

static void freelist_insert(am_freelist_state_t *s, am_heap_block_header_t *b) {
    b->prev_free = NULL;
    b->next_free = s->free_list_head;
    if (s->free_list_head) {
        s->free_list_head->prev_free = b;
    }
    s->free_list_head = b;
}

static void freelist_remove(am_freelist_state_t *s, am_heap_block_header_t *b) {
    if (b->prev_free) {
        b->prev_free->next_free = b->next_free;
    } else {
        s->free_list_head = b->next_free;
    }
    if (b->next_free) {
        b->next_free->prev_free = b->prev_free;
    }
    b->prev_free = NULL;
    b->next_free = NULL;
}

static void freelist_coalesce(am_freelist_state_t *s, am_heap_block_header_t *b) {
    size_t new_size = block_real_size(b);
    am_heap_block_header_t *prev = block_prev(s, b);
    am_heap_block_header_t *next = block_next(s, b);

    if (prev && !block_is_used(prev)) {
        freelist_remove(s, prev);
        new_size += block_real_size(prev);
        b = prev;
    }
    if (next && !block_is_used(next)) {
        freelist_remove(s, next);
        new_size += block_real_size(next);
    }

    block_set_size(b, new_size, false);
    b->live = false;

    am_heap_block_header_t *new_next = block_next(s, b);
    if (new_next) {
        new_next->prev_size = new_size;
    }
    freelist_insert(s, b);
}

static void *freelist_malloc(void *state, size_t size) {
    am_freelist_state_t *s = (am_freelist_state_t *)state;
    if (size == 0 || !s) return NULL;

    size_t needed = AM_ALIGN_UP(AM_HEAP_HEADER_SIZE + size, AM_ALLOC_ALIGN);
    if (needed < AM_BLOCK_MIN_SIZE) needed = AM_BLOCK_MIN_SIZE;

    am_heap_block_header_t *cur = s->free_list_head;
    while (cur) {
        size_t cur_size = block_real_size(cur);
        if (cur_size >= needed) {
            freelist_remove(s, cur);
            size_t remainder = cur_size - needed;
            if (remainder >= AM_BLOCK_MIN_SIZE) {
                am_heap_block_header_t *split = (am_heap_block_header_t *)((uint8_t *)cur + needed);
                block_set_size(split, remainder, false);
                split->prev_size = needed;
                split->live = false;
                am_heap_block_header_t *split_next = block_next(s, split);
                if (split_next) split_next->prev_size = remainder;
                freelist_insert(s, split);
                block_set_size(cur, needed, true);
            } else {
                block_set_size(cur, cur_size, true);
            }
            cur->live = false;
            s->used_bytes += block_real_size(cur);
            return block_payload(cur);
        }
        cur = cur->next_free;
    }
    return NULL;
}

static void *freelist_calloc(void *state, size_t size) {
    void *p = freelist_malloc(state, size);
    if (p) memset(p, 0, size);
    return p;
}

static void freelist_free(void *state, void *ptr) {
    am_freelist_state_t *s = (am_freelist_state_t *)state;
    if (!ptr || !s) return;

    am_heap_block_header_t *b = block_from_payload(ptr);
    if (!block_is_used(b)) return; /* 重复释放 */

    s->used_bytes -= block_real_size(b);
    block_set_size(b, block_real_size(b), false);
    b->live = false;
    freelist_coalesce(s, b);
}

static void *freelist_realloc(void *state, void *ptr, size_t size) {
    if (ptr == NULL) return freelist_malloc(state, size);
    if (size == 0) {
        freelist_free(state, ptr);
        return NULL;
    }

    am_heap_block_header_t *b = block_from_payload(ptr);
    size_t old_payload = block_real_size(b) - AM_HEAP_HEADER_SIZE;
    if (size <= old_payload) return ptr;

    void *new_ptr = freelist_malloc(state, size);
    if (new_ptr) {
        size_t copy = old_payload < size ? old_payload : size;
        memcpy(new_ptr, ptr, copy);
        freelist_free(state, ptr);
    }
    return new_ptr;
}

static void freelist_destroy(void *state) {
    (void)state;
}

static const am_allocator_vtable_t freelist_vtable = {
    freelist_malloc,
    freelist_calloc,
    freelist_realloc,
    freelist_free,
    freelist_destroy
};

/* =============================================================================
 * 内存池：统一管理 VM 工作区与堆区
 * ============================================================================ */

struct am_allocator_pool_t {
    uint8_t *base;
    size_t total_size;
    size_t boundary;

    am_bump_state_t vm_state;
    am_allocator_t vm_alloc;

    am_freelist_state_t heap_state;
    am_allocator_t heap_alloc;
};

static void pool_init_heap(am_allocator_pool_t *pool) {
    am_freelist_state_t *s = &pool->heap_state;
    s->base = pool->base;
    s->capacity = pool->boundary;
    s->used_bytes = 0;
    s->free_list_head = NULL;

    am_heap_block_header_t *b = (am_heap_block_header_t *)s->base;
    block_set_size(b, s->capacity, false);
    b->prev_size = 0;
    b->next_free = NULL;
    b->prev_free = NULL;
    b->live = false;
    s->free_list_head = b;
}

static void pool_init_vm(am_allocator_pool_t *pool) {
    am_bump_state_t *s = &pool->vm_state;
    s->base = pool->base + pool->boundary;
    s->top = s->base;
    s->end = pool->base + pool->total_size;
}

am_allocator_pool_t *am_allocator_pool_create(size_t total_size) {
    if (total_size < 2 * 1024 * 1024) total_size = 2 * 1024 * 1024;

    am_allocator_pool_t *pool = (am_allocator_pool_t *)malloc(sizeof(am_allocator_pool_t));
    if (!pool) return NULL;

    pool->base = (uint8_t *)malloc(total_size);
    if (!pool->base) {
        free(pool);
        return NULL;
    }
    pool->total_size = total_size;
    pool->boundary = (total_size / 2) & ~(AM_ALLOC_ALIGN - 1);

    pool_init_heap(pool);
    pool_init_vm(pool);

    pool->heap_alloc.vtable = &freelist_vtable;
    pool->heap_alloc.state = &pool->heap_state;

    pool->vm_alloc.vtable = &bump_vtable;
    pool->vm_alloc.state = &pool->vm_state;

    return pool;
}

void am_allocator_pool_destroy(am_allocator_pool_t *pool) {
    if (!pool) return;
    if (pool->base) {
        free(pool->base);
        pool->base = NULL;
    }
    free(pool);
}

am_allocator_t *am_allocator_pool_get_vm(am_allocator_pool_t *pool) {
    if (!pool) return NULL;
    return &pool->vm_alloc;
}

am_allocator_t *am_allocator_pool_get_heap(am_allocator_pool_t *pool) {
    if (!pool) return NULL;
    return &pool->heap_alloc;
}

void am_allocator_pool_reset_vm(am_allocator_pool_t *pool) {
    if (!pool) return;
    pool->vm_state.top = pool->vm_state.base;
}

void am_allocator_pool_reset_heap(am_allocator_pool_t *pool) {
    if (!pool) return;
    pool_init_heap(pool);
}

size_t am_allocator_pool_total_size(const am_allocator_pool_t *pool) {
    return pool ? pool->total_size : 0;
}

size_t am_allocator_pool_vm_used(const am_allocator_pool_t *pool) {
    if (!pool) return 0;
    return (size_t)(pool->vm_state.top - pool->vm_state.base);
}

size_t am_allocator_pool_heap_used(const am_allocator_pool_t *pool) {
    if (!pool) return 0;
    return pool->heap_state.used_bytes;
}

/* =============================================================================
 * 堆区压缩：在 GC 安全点移动存活对象，更新 heap 表中的指针
 * ============================================================================ */

typedef struct {
    am_heap_block_header_t *block;
    am_map_entry_t *slot;
} live_entry_t;

typedef struct {
    void *old_ptr;
    void *new_ptr;
} reloc_entry_t;

static int cmp_live_entry(const void *a, const void *b) {
    const live_entry_t *ea = (const live_entry_t *)a;
    const live_entry_t *eb = (const live_entry_t *)b;
    if (ea->block < eb->block) return -1;
    if (ea->block > eb->block) return 1;
    return 0;
}

static int cmp_slot_ptr(const void *a, const void *b) {
    am_map_entry_t *const *sa = (am_map_entry_t *const *)a;
    am_map_entry_t *const *sb = (am_map_entry_t *const *)b;
    if (*sa < *sb) return -1;
    if (*sa > *sb) return 1;
    return 0;
}

int32_t am_allocator_heap_compact(am_allocator_t *heap_alloc, am_heap_t *heap) {
    if (!heap_alloc || !heap_alloc->state || !heap || !heap->table) return -1;
    am_freelist_state_t *s = (am_freelist_state_t *)heap_alloc->state;

    size_t cap = heap->table->capacity;
    live_entry_t *entries = NULL;
    size_t count = 0;
    size_t entries_cap = 0;

    /* 第一遍：收集所有被 handle 引用的存活对象（仅处理物理上位于堆区内的对象） */
    for (size_t i = 0; i < cap; i++) {
        am_value_t key = heap->table->slots[i].key;
        if (key == AM_MAP_KEY_EMPTY || key == AM_MAP_KEY_TOMBSTONE) continue;
        am_value_t v = heap->table->slots[i].value;
        if (!am_value_is_ptr(v)) continue;

        am_heap_block_header_t *b = block_from_payload(am_value_to_ptr(v));
        if ((uint8_t *)b < s->base || (uint8_t *)b >= s->base + s->capacity) continue;

        if (!b->live) {
            b->live = true;
            if (count >= entries_cap) {
                entries_cap = entries_cap ? entries_cap * 2 : 64;
                live_entry_t *tmp = (live_entry_t *)realloc(entries, entries_cap * sizeof(live_entry_t));
                if (!tmp) {
                    free(entries);
                    return -1;
                }
                entries = tmp;
            }
            entries[count].block = b;
            entries[count].slot = &heap->table->slots[i];
            count++;
        }
    }

    if (count == 0) {
        am_heap_block_header_t *b = (am_heap_block_header_t *)s->base;
        block_set_size(b, s->capacity, false);
        b->prev_size = 0;
        b->next_free = NULL;
        b->prev_free = NULL;
        b->live = false;
        s->free_list_head = b;
        s->used_bytes = 0;
        free(entries);
        return 0;
    }

    qsort(entries, count, sizeof(live_entry_t), cmp_live_entry);

    am_map_entry_t **primary_slots = (am_map_entry_t **)malloc(count * sizeof(am_map_entry_t *));
    if (!primary_slots) {
        free(entries);
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        primary_slots[i] = entries[i].slot;
    }
    qsort(primary_slots, count, sizeof(am_map_entry_t *), cmp_slot_ptr);

    reloc_entry_t *reloc = (reloc_entry_t *)malloc(count * sizeof(reloc_entry_t));
    if (!reloc) {
        free(entries);
        free(primary_slots);
        return -1;
    }

    uint8_t *dest = s->base;
    size_t prev_size = 0;
    for (size_t i = 0; i < count; i++) {
        am_heap_block_header_t *b = entries[i].block;
        size_t sz = block_real_size(b);
        void *old_payload = block_payload(b);

        if ((uint8_t *)b != dest) {
            memmove(dest, b, sz);
        }

        am_heap_block_header_t *newb = (am_heap_block_header_t *)dest;
        block_set_size(newb, sz, true);
        newb->prev_size = prev_size;
        newb->live = false;

        void *new_payload = block_payload(newb);
        reloc[i].old_ptr = old_payload;
        reloc[i].new_ptr = new_payload;
        entries[i].slot->value = am_make_value_of_ptr((am_object_t *)new_payload);

        dest += sz;
        prev_size = sz;
    }

    /* 第二遍：更新非主 slot 中仍指向旧地址的指针。
     * 主 slot 已在移动循环中更新；若其新地址恰好等于某个旧地址，
     * 必须避免在此处被再次更新为另一个对象的新地址。 */
    for (size_t i = 0; i < cap; i++) {
        am_value_t key = heap->table->slots[i].key;
        if (key == AM_MAP_KEY_EMPTY || key == AM_MAP_KEY_TOMBSTONE) continue;
        am_value_t v = heap->table->slots[i].value;
        if (!am_value_is_ptr(v)) continue;

        am_map_entry_t *slot = &heap->table->slots[i];
        am_map_entry_t *slot_key = slot;
        if (bsearch(&slot_key, primary_slots, count, sizeof(am_map_entry_t *), cmp_slot_ptr) != NULL) {
            continue;
        }

        void *old_ptr = am_value_to_ptr(v);
        size_t lo = 0, hi = count;
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (reloc[mid].old_ptr < old_ptr) lo = mid + 1;
            else hi = mid;
        }
        if (lo < count && reloc[lo].old_ptr == old_ptr) {
            slot->value = am_make_value_of_ptr((am_object_t *)reloc[lo].new_ptr);
        }
    }

    size_t free_size = (s->base + s->capacity) - dest;
    if (free_size > 0) {
        am_heap_block_header_t *freeb = (am_heap_block_header_t *)dest;
        block_set_size(freeb, free_size, false);
        freeb->prev_size = prev_size;
        freeb->next_free = NULL;
        freeb->prev_free = NULL;
        freeb->live = false;
        s->free_list_head = freeb;
    } else {
        s->free_list_head = NULL;
    }
    s->used_bytes = (size_t)(dest - s->base);

    free(entries);
    free(primary_slots);
    free(reloc);
    return 0;
}
