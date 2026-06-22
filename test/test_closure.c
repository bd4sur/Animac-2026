#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// gcc -o test_closure test_closure.c -Wall -Wextra

// object.h 中部分内联函数存在未使用参数，属于已有代码的警告；
// 仅在本测试文件中临时忽略，不影响 closure.h 的实现。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "closure.h"
#pragma GCC diagnostic pop

// ===============================================================================
// 基础设施：实现一个委托给系统 malloc/free/realloc 的简单抽象分配器
// ===============================================================================

static void* test_malloc(void *state, size_t size);
static void* test_calloc(void *state, size_t size);
static void* test_realloc(void *state, void *ptr, size_t size);
static void  test_free(void *state, void *ptr);
static void  test_destroy(void *state);

static void* test_malloc(void *state, size_t size) {
    (void)state;
    return malloc(size);
}

static void* test_calloc(void *state, size_t size) {
    (void)state;
    return calloc(1, size);
}

static void* test_realloc(void *state, void *ptr, size_t size) {
    (void)state;
    return realloc(ptr, size);
}

static void test_free(void *state, void *ptr) {
    (void)state;
    free(ptr);
}

static void test_destroy(void *state) {
    (void)state;
}

static const am_allocator_vtable_t test_allocator_vtable = {
    test_malloc,
    test_calloc,
    test_realloc,
    test_free,
    test_destroy
};

static am_allocator_t test_allocator = { &test_allocator_vtable, NULL };

// ===============================================================================
// 测试用例
// ===============================================================================

static void test_create_and_basic_properties(void) {
    printf("test_create_and_basic_properties ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 42, 7, 0);
    assert(closure != NULL);
    assert(closure->base.type == AM_OBJECT_TYPE_CLOSURE);
    assert(closure->iaddr == 42);
    assert(closure->parent == 7);
    assert(closure->length == 0);
    assert(closure->capacity == 16);

    am_obj_closure_t *closure2 = am_closure_create(&test_allocator, 0, 0, 4);
    assert(closure2 != NULL);
    assert(closure2->capacity == 4);

    am_closure_destroy(&test_allocator, closure);
    am_closure_destroy(&test_allocator, closure2);
    printf("OK\n");
}

static void test_bound_vars(void) {
    printf("test_bound_vars ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 1, 2, 8);
    assert(closure != NULL);

    am_value_t v1 = am_make_value_of_uint(100);
    closure = am_closure_init_bound_var(&test_allocator, closure, 1, v1);
    assert(closure != NULL);
    assert(am_closure_has_bound_var(&test_allocator, closure, 1) == 0);
    assert(am_closure_get_bound_var(&test_allocator, closure, 1) == v1);
    assert(am_closure_is_dirty_var(&test_allocator, closure, 1) == -1);

    am_value_t v2 = am_make_value_of_uint(200);
    closure = am_closure_set_bound_var(&test_allocator, closure, 1, v2);
    assert(closure != NULL);
    assert(am_closure_get_bound_var(&test_allocator, closure, 1) == v2);
    assert(am_closure_is_dirty_var(&test_allocator, closure, 1) == 0);

    assert(am_closure_get_bound_var(&test_allocator, closure, 999) == AM_VALUE_UNDEFINED);
    assert(am_closure_has_bound_var(&test_allocator, closure, 999) == -1);

    am_closure_destroy(&test_allocator, closure);
    printf("OK\n");
}

static void test_free_vars(void) {
    printf("test_free_vars ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 1, 2, 8);
    assert(closure != NULL);

    am_value_t v1 = am_make_value_of_int(-10);
    closure = am_closure_init_free_var(&test_allocator, closure, 5, v1);
    assert(closure != NULL);
    assert(am_closure_has_free_var(&test_allocator, closure, 5) == 0);
    assert(am_closure_get_free_var(&test_allocator, closure, 5) == v1);
    assert(am_closure_is_dirty_var(&test_allocator, closure, 5) == -1);

    am_value_t v2 = am_make_value_of_int(-20);
    closure = am_closure_set_free_var(&test_allocator, closure, 5, v2);
    assert(closure != NULL);
    assert(am_closure_get_free_var(&test_allocator, closure, 5) == v2);
    assert(am_closure_is_dirty_var(&test_allocator, closure, 5) == 0);

    assert(am_closure_get_free_var(&test_allocator, closure, 999) == AM_VALUE_UNDEFINED);
    assert(am_closure_has_free_var(&test_allocator, closure, 999) == -1);

    am_closure_destroy(&test_allocator, closure);
    printf("OK\n");
}

static void test_bound_and_free_same_varid(void) {
    printf("test_bound_and_free_same_varid ... ");

    // 虽然语义上不会同时出现，但线性表按 type 区分，需保证二者互不干扰
    am_obj_closure_t *closure = am_closure_create(&test_allocator, 0, 0, 8);
    assert(closure != NULL);

    am_value_t bound_v = am_make_value_of_uint(1);
    am_value_t free_v = am_make_value_of_uint(2);

    closure = am_closure_init_bound_var(&test_allocator, closure, 10, bound_v);
    assert(closure != NULL);
    closure = am_closure_init_free_var(&test_allocator, closure, 10, free_v);
    assert(closure != NULL);

    assert(am_closure_get_bound_var(&test_allocator, closure, 10) == bound_v);
    assert(am_closure_get_free_var(&test_allocator, closure, 10) == free_v);
    assert(am_closure_has_bound_var(&test_allocator, closure, 10) == 0);
    assert(am_closure_has_free_var(&test_allocator, closure, 10) == 0);

    am_closure_destroy(&test_allocator, closure);
    printf("OK\n");
}

static void test_resize(void) {
    printf("test_resize ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 0, 0, 4);
    assert(closure != NULL);
    assert(closure->capacity == 4);

    const int N = 100;
    for (int i = 0; i < N; i++) {
        am_value_t v = am_make_value_of_uint((uint32_t)i);
        closure = am_closure_init_bound_var(&test_allocator, closure, (am_varid_t)i, v);
        assert(closure != NULL);
    }

    assert(closure->length == (uint32_t)N);
    assert(closure->capacity >= (uint32_t)N);

    for (int i = 0; i < N; i++) {
        am_value_t expected = am_make_value_of_uint((uint32_t)i);
        assert(am_closure_has_bound_var(&test_allocator, closure, (am_varid_t)i) == 0);
        assert(am_closure_get_bound_var(&test_allocator, closure, (am_varid_t)i) == expected);
    }

    am_closure_destroy(&test_allocator, closure);
    printf("OK\n");
}

static void test_copy(void) {
    printf("test_copy ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 123, 456, 8);
    assert(closure != NULL);

    for (int i = 0; i < 5; i++) {
        am_value_t v = am_make_value_of_uint((uint32_t)(i + 1));
        closure = am_closure_init_bound_var(&test_allocator, closure, (am_varid_t)i, v);
        assert(closure != NULL);
    }
    closure = am_closure_set_bound_var(&test_allocator, closure, 2, am_make_value_of_uint(99));
    assert(closure != NULL);

    for (int i = 10; i < 13; i++) {
        am_value_t v = am_make_value_of_int(-(i));
        closure = am_closure_init_free_var(&test_allocator, closure, (am_varid_t)i, v);
        assert(closure != NULL);
    }

    am_obj_closure_t *copy = am_closure_copy(&test_allocator, closure);
    assert(copy != NULL);
    assert(copy != closure);
    assert(copy->iaddr == closure->iaddr);
    assert(copy->parent == closure->parent);
    assert(copy->length == closure->length);
    assert(copy->capacity == closure->capacity);

    for (int i = 0; i < 5; i++) {
        am_value_t expected = (i == 2) ? am_make_value_of_uint(99) : am_make_value_of_uint((uint32_t)(i + 1));
        assert(am_closure_get_bound_var(&test_allocator, copy, (am_varid_t)i) == expected);
    }
    assert(am_closure_is_dirty_var(&test_allocator, copy, 2) == 0);

    for (int i = 10; i < 13; i++) {
        am_value_t expected = am_make_value_of_int(-(i));
        assert(am_closure_get_free_var(&test_allocator, copy, (am_varid_t)i) == expected);
    }

    // 拷贝应独立：修改副本不应影响原闭包
    copy = am_closure_set_bound_var(&test_allocator, copy, 0, am_make_value_of_uint(9999));
    assert(copy != NULL);
    assert(am_closure_get_bound_var(&test_allocator, copy, 0) == am_make_value_of_uint(9999));
    assert(am_closure_get_bound_var(&test_allocator, closure, 0) == am_make_value_of_uint(1));

    am_closure_destroy(&test_allocator, closure);
    am_closure_destroy(&test_allocator, copy);
    printf("OK\n");
}

static void test_init_overwrite_clears_dirty(void) {
    printf("test_init_overwrite_clears_dirty ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 0, 0, 8);
    assert(closure != NULL);

    closure = am_closure_init_bound_var(&test_allocator, closure, 1, am_make_value_of_uint(1));
    assert(closure != NULL);
    assert(am_closure_is_dirty_var(&test_allocator, closure, 1) == -1);

    closure = am_closure_set_bound_var(&test_allocator, closure, 1, am_make_value_of_uint(2));
    assert(closure != NULL);
    assert(am_closure_is_dirty_var(&test_allocator, closure, 1) == 0);

    closure = am_closure_init_bound_var(&test_allocator, closure, 1, am_make_value_of_uint(3));
    assert(closure != NULL);
    assert(am_closure_get_bound_var(&test_allocator, closure, 1) == am_make_value_of_uint(3));
    assert(am_closure_is_dirty_var(&test_allocator, closure, 1) == -1);

    am_closure_destroy(&test_allocator, closure);
    printf("OK\n");
}

static void test_capacity_zero_defaults(void) {
    printf("test_capacity_zero_defaults ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 0, 0, 0);
    assert(closure != NULL);
    assert(closure->capacity == 16);
    assert(closure->length == 0);

    // 触发扩容路径
    for (int i = 0; i < 20; i++) {
        closure = am_closure_init_bound_var(&test_allocator, closure, (am_varid_t)i, am_make_value_of_uint((uint32_t)i));
        assert(closure != NULL);
    }

    am_closure_destroy(&test_allocator, closure);
    printf("OK\n");
}

static int closure_equal(am_obj_closure_t *a, am_obj_closure_t *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->base.type != b->base.type) return 0;
    if (a->iaddr != b->iaddr) return 0;
    if (a->parent != b->parent) return 0;
    if (a->length != b->length) return 0;
    for (size_t i = 0; i < a->length; i++) {
        if (a->bindings[i].varid != b->bindings[i].varid) return 0;
        if (a->bindings[i].type != b->bindings[i].type) return 0;
        if (a->bindings[i].dirty_flag != b->bindings[i].dirty_flag) return 0;
        if (a->bindings[i].value != b->bindings[i].value) return 0;
    }
    return 1;
}

static void test_dump_load_empty(void) {
    printf("test_dump_load_empty ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 42, 7, 8);
    assert(closure != NULL);

    size_t size = am_closure_dump(&test_allocator, closure, NULL, 0);
    assert(size != SIZE_MAX);
    assert(size == sizeof(am_obj_closure_t));

    uint8_t *buffer = (uint8_t *)malloc(size);
    assert(buffer != NULL);
    assert(am_closure_dump(&test_allocator, closure, buffer, 0) == size);

    am_obj_closure_t *loaded = am_closure_load(&test_allocator, buffer, 0);
    assert(loaded != NULL);
    assert(closure_equal(closure, loaded));
    assert(loaded->base.type == AM_OBJECT_TYPE_CLOSURE);
    assert(loaded->capacity == 0);
    assert(loaded->length == 0);
    assert(loaded->iaddr == 42);
    assert(loaded->parent == 7);

    free(buffer);
    am_closure_destroy(&test_allocator, closure);
    am_closure_destroy(&test_allocator, loaded);
    printf("OK\n");
}

static void test_dump_load_with_bindings(void) {
    printf("test_dump_load_with_bindings ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 100, 200, 8);
    assert(closure != NULL);

    closure = am_closure_init_bound_var(&test_allocator, closure, 1, am_make_value_of_uint(10));
    assert(closure != NULL);
    closure = am_closure_init_bound_var(&test_allocator, closure, 2, am_make_value_of_uint(20));
    assert(closure != NULL);
    closure = am_closure_set_bound_var(&test_allocator, closure, 2, am_make_value_of_uint(25));
    assert(closure != NULL);
    closure = am_closure_init_free_var(&test_allocator, closure, 10, am_make_value_of_int(-5));
    assert(closure != NULL);

    size_t size = am_closure_dump(&test_allocator, closure, NULL, 0);
    assert(size != SIZE_MAX);
    assert(size == sizeof(am_obj_closure_t) + 3 * sizeof(am_binding_t));

    uint8_t *buffer = (uint8_t *)malloc(size);
    assert(buffer != NULL);
    assert(am_closure_dump(&test_allocator, closure, buffer, 0) == size);

    am_obj_closure_t *loaded = am_closure_load(&test_allocator, buffer, 0);
    assert(loaded != NULL);
    assert(closure_equal(closure, loaded));
    assert(loaded->base.type == AM_OBJECT_TYPE_CLOSURE);
    assert(loaded->capacity == 3);
    assert(loaded->length == 3);
    assert(loaded->iaddr == 100);
    assert(loaded->parent == 200);
    assert(am_closure_is_dirty_var(&test_allocator, loaded, 2) == 0);

    free(buffer);
    am_closure_destroy(&test_allocator, closure);
    am_closure_destroy(&test_allocator, loaded);
    printf("OK\n");
}

static void test_dump_load_with_offset(void) {
    printf("test_dump_load_with_offset ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 1, 2, 4);
    assert(closure != NULL);
    closure = am_closure_init_bound_var(&test_allocator, closure, 0, am_make_value_of_uint(99));
    assert(closure != NULL);

    size_t size = am_closure_dump(&test_allocator, closure, NULL, 0);
    assert(size != SIZE_MAX);

    uint8_t *buffer = (uint8_t *)malloc(size + 16);
    assert(buffer != NULL);
    memset(buffer, 0xEE, size + 16);

    assert(am_closure_dump(&test_allocator, closure, buffer, 16) == size);

    am_obj_closure_t *loaded = am_closure_load(&test_allocator, buffer, 16);
    assert(loaded != NULL);
    assert(closure_equal(closure, loaded));

    free(buffer);
    am_closure_destroy(&test_allocator, closure);
    am_closure_destroy(&test_allocator, loaded);
    printf("OK\n");
}

static void test_load_invalid_type(void) {
    printf("test_load_invalid_type ... ");

    uint8_t buffer[64];
    memset(buffer, 0, sizeof(buffer));
    am_obj_closure_t *fake = (am_obj_closure_t *)buffer;
    fake->base.type = AM_OBJECT_TYPE_LIST;
    fake->length = 0;

    assert(am_closure_load(&test_allocator, buffer, 0) == NULL);
    printf("OK\n");
}

static void test_load_null_buffer(void) {
    printf("test_load_null_buffer ... ");

    assert(am_closure_load(&test_allocator, NULL, 0) == NULL);
    printf("OK\n");
}

// ===============================================================================
// 入口
// ===============================================================================

int main(void) {
    printf("Running closure tests...\n");

    test_create_and_basic_properties();
    test_bound_vars();
    test_free_vars();
    test_bound_and_free_same_varid();
    test_resize();
    test_copy();
    test_init_overwrite_clears_dirty();
    test_capacity_zero_defaults();
    test_dump_load_empty();
    test_dump_load_with_bindings();
    test_dump_load_with_offset();
    test_load_invalid_type();
    test_load_null_buffer();

    printf("All closure tests passed.\n");
    return 0;
}
