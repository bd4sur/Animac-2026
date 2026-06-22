#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "object.h"
#include "list.h"
#pragma GCC diagnostic pop


// ===============================================================================
// 基础设施：基于内存池的简单测试分配器（bump allocator）
// ===============================================================================

#define TEST_POOL_SIZE (1024 * 1024)

typedef struct test_allocator_state_t {
    uint8_t *base;
    size_t offset;
    size_t capacity;
} test_allocator_state_t;

static test_allocator_state_t test_allocator_state;

static void* test_malloc(void *state, size_t size) {
    test_allocator_state_t *s = (test_allocator_state_t *)state;
    if (size == 0) return NULL;
    size_t aligned_size = (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
    if (s->offset + aligned_size > s->capacity) {
        fprintf(stderr, "test_malloc FAIL: need %zu, have %zu\n", size, s->capacity - s->offset);
        return NULL;
    }
    void *p = s->base + s->offset;
    s->offset += aligned_size;
    return p;
}

static void* test_calloc(void *state, size_t size) {
    void *p = test_malloc(state, size);
    if (p) memset(p, 0, size);
    return p;
}

static void* test_realloc(void *state, void *ptr, size_t size) {
    if (ptr == NULL) return test_malloc(state, size);
    void *new_ptr = test_malloc(state, size);
    if (new_ptr && ptr) {
        memcpy(new_ptr, ptr, size);
    }
    return new_ptr;
}

static void test_free(void *state, void *ptr) {
    (void)state;
    (void)ptr;
}

static am_allocator_t test_allocator = {
    .vtable = &(am_allocator_vtable_t){
        .malloc = test_malloc,
        .calloc = test_calloc,
        .realloc = test_realloc,
        .free = test_free,
        .destroy = NULL,
    },
    .state = &test_allocator_state,
};

static void test_allocator_reset(void) {
    test_allocator_state.offset = 0;
}


// ===============================================================================
// 辅助函数
// ===============================================================================

static int list_equal(am_list_t *a, am_list_t *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->length != b->length) return 0;
    if (a->type != b->type) return 0;
    if (a->parent != b->parent) return 0;
    if (a->base.type != b->base.type) return 0;
    for (size_t i = 0; i < a->length; i++) {
        if (a->children[i] != b->children[i]) return 0;
    }
    return 1;
}


// ===============================================================================
// 测试用例
// ===============================================================================

static void test_dump_load_empty(void) {
    printf("test_dump_load_empty ... ");

    am_list_t *lst = am_list_create(&test_allocator, 8, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    assert(lst != NULL);

    size_t size = am_list_dump(&test_allocator, lst, NULL, 0);
    assert(size != SIZE_MAX);
    assert(size == sizeof(am_list_t));

    uint8_t *buffer = (uint8_t *)malloc(size);
    assert(buffer != NULL);
    assert(am_list_dump(&test_allocator, lst, buffer, 0) == size);

    am_list_t *loaded = am_list_load(&test_allocator, buffer, 0);
    assert(loaded != NULL);
    assert(list_equal(lst, loaded));
    assert(loaded->base.type == AM_OBJECT_TYPE_LIST);
    assert(loaded->capacity == 0);
    assert(loaded->length == 0);

    free(buffer);
    test_allocator_reset();
    printf("OK\n");
}

static void test_dump_load_with_children(void) {
    printf("test_dump_load_with_children ... ");

    am_list_t *lst = am_list_create(&test_allocator, 8, AM_LIST_TYPE_APPLICATION, 42);
    assert(lst != NULL);

    for (int i = 0; i < 5; i++) {
        lst = am_list_push(&test_allocator, lst, am_make_value_of_uint((uint32_t)i));
        assert(lst != NULL);
    }

    size_t size = am_list_dump(&test_allocator, lst, NULL, 0);
    assert(size != SIZE_MAX);
    assert(size == sizeof(am_list_t) + 5 * sizeof(am_value_t));

    uint8_t *buffer = (uint8_t *)malloc(size);
    assert(buffer != NULL);
    assert(am_list_dump(&test_allocator, lst, buffer, 0) == size);

    am_list_t *loaded = am_list_load(&test_allocator, buffer, 0);
    assert(loaded != NULL);
    assert(list_equal(lst, loaded));
    assert(loaded->base.type == AM_OBJECT_TYPE_LIST);
    assert(loaded->capacity == 5);
    assert(loaded->length == 5);
    assert(loaded->type == AM_LIST_TYPE_APPLICATION);
    assert(loaded->parent == 42);

    free(buffer);
    test_allocator_reset();
    printf("OK\n");
}

static void test_dump_load_with_offset(void) {
    printf("test_dump_load_with_offset ... ");

    am_list_t *lst = am_list_create(&test_allocator, 4, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    assert(lst != NULL);
    lst = am_list_push(&test_allocator, lst, am_make_value_of_int(-7));
    assert(lst != NULL);

    size_t size = am_list_dump(&test_allocator, lst, NULL, 0);
    assert(size != SIZE_MAX);

    uint8_t *buffer = (uint8_t *)malloc(size + 10);
    assert(buffer != NULL);
    memset(buffer, 0xBB, size + 10);

    assert(am_list_dump(&test_allocator, lst, buffer, 6) == size);

    am_list_t *loaded = am_list_load(&test_allocator, buffer, 6);
    assert(loaded != NULL);
    assert(list_equal(lst, loaded));

    free(buffer);
    test_allocator_reset();
    printf("OK\n");
}

static void test_load_invalid_type(void) {
    printf("test_load_invalid_type ... ");

    uint8_t buffer[64];
    memset(buffer, 0, sizeof(buffer));
    am_list_t *fake = (am_list_t *)buffer;
    fake->base.type = AM_OBJECT_TYPE_MAP;
    fake->length = 0;

    assert(am_list_load(&test_allocator, buffer, 0) == NULL);

    test_allocator_reset();
    printf("OK\n");
}

static void test_load_null_buffer(void) {
    printf("test_load_null_buffer ... ");

    assert(am_list_load(&test_allocator, NULL, 0) == NULL);

    test_allocator_reset();
    printf("OK\n");
}


// ===============================================================================
// 主函数
// ===============================================================================

int main(void) {
    test_allocator_state.base = (uint8_t *)malloc(TEST_POOL_SIZE);
    assert(test_allocator_state.base != NULL);
    test_allocator_state.offset = 0;
    test_allocator_state.capacity = TEST_POOL_SIZE;

    printf("Running list dump/load tests...\n");

    test_dump_load_empty();
    test_dump_load_with_children();
    test_dump_load_with_offset();
    test_load_invalid_type();
    test_load_null_buffer();

    printf("All list dump/load tests passed.\n");

    free(test_allocator_state.base);
    return 0;
}
