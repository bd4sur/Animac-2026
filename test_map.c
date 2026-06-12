#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// gcc -o test_map test_map.c -Wall -Wextra

// object.h 中部分内联函数存在未使用参数，属于已有代码的警告；
// 仅在本测试文件中临时忽略，不影响 map.h 的实现。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "map.h"
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
// 测试用辅助结构与回调
// ===============================================================================

static int g_iter_count = 0;
static int g_iter_sum = 0;

static void reset_iter_stats(void) {
    g_iter_count = 0;
    g_iter_sum = 0;
}

static void sum_value_callback(am_value_t key, am_value_t value, void *user_data) {
    (void)key;
    (void)user_data;
    if (am_value_is_uint(value)) {
        g_iter_sum += (int)am_value_to_uint(value);
    }
    g_iter_count++;
}

// ===============================================================================
// 测试用例
// ===============================================================================

static void test_create_and_basic_properties(void) {
    printf("test_create_and_basic_properties ... ");

    am_map_t *map = am_map_create(&test_allocator, 5);
    assert(map != NULL);
    assert(am_map_capacity(&test_allocator, map) == 8);
    assert(am_map_size(&test_allocator, map) == 0);

    am_map_t *map2 = am_map_create(&test_allocator, 0);
    assert(map2 != NULL);
    assert(am_map_capacity(&test_allocator, map2) == 8);

    am_map_t *map3 = am_map_create(&test_allocator, 17);
    assert(map3 != NULL);
    assert(am_map_capacity(&test_allocator, map3) == 32);

    am_map_destroy(&test_allocator, map);
    am_map_destroy(&test_allocator, map2);
    am_map_destroy(&test_allocator, map3);
    printf("OK\n");
}

static void test_set_get_contains_uint(void) {
    printf("test_set_get_contains_uint ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);

    am_value_t k1 = am_make_value_of_uint(42);
    am_value_t v1 = am_make_value_of_uint(100);
    map = am_map_set(&test_allocator, map, k1, v1);
    assert(map != NULL);
    assert(am_map_size(&test_allocator, map) == 1);
    assert(am_map_contains(&test_allocator, map, k1) == 1);
    assert(am_value_equal(am_map_get(&test_allocator, map, k1), v1));

    am_value_t k2 = am_make_value_of_uint(43);
    assert(am_map_contains(&test_allocator, map, k2) == 0);
    assert(am_value_equal(am_map_get(&test_allocator, map, k2), AM_VALUE_NULL));

    am_value_t v1_new = am_make_value_of_uint(200);
    map = am_map_set(&test_allocator, map, k1, v1_new);
    assert(map != NULL);
    assert(am_map_size(&test_allocator, map) == 1);
    assert(am_value_equal(am_map_get(&test_allocator, map, k1), v1_new));

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_different_value_types(void) {
    printf("test_different_value_types ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);

    am_value_t k_uint = am_make_value_of_uint(1);
    am_value_t k_int  = am_make_value_of_int(-1);
    am_value_t k_sym  = am_make_value_of_symbol(42);
    am_value_t k_bool = am_make_value_of_boolean(1);

    am_value_t v1 = am_make_value_of_uint(101);
    am_value_t v2 = am_make_value_of_int(-101);
    am_value_t v3 = am_make_value_of_symbol(999);
    am_value_t v4 = am_make_value_of_boolean(0);

    map = am_map_set(&test_allocator, map, k_uint, v1); assert(map != NULL);
    map = am_map_set(&test_allocator, map, k_int,  v2); assert(map != NULL);
    map = am_map_set(&test_allocator, map, k_sym,  v3); assert(map != NULL);
    map = am_map_set(&test_allocator, map, k_bool, v4); assert(map != NULL);

    assert(am_map_size(&test_allocator, map) == 4);
    assert(am_value_equal(am_map_get(&test_allocator, map, k_uint), v1));
    assert(am_value_equal(am_map_get(&test_allocator, map, k_int),  v2));
    assert(am_value_equal(am_map_get(&test_allocator, map, k_sym),  v3));
    assert(am_value_equal(am_map_get(&test_allocator, map, k_bool), v4));

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_delete_and_tombstones(void) {
    printf("test_delete_and_tombstones ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);

    am_value_t k1 = am_make_value_of_uint(1);
    am_value_t k2 = am_make_value_of_uint(2);
    am_value_t k3 = am_make_value_of_uint(3);

    map = am_map_set(&test_allocator, map, k1, am_make_value_of_uint(10)); assert(map != NULL);
    map = am_map_set(&test_allocator, map, k2, am_make_value_of_uint(20)); assert(map != NULL);
    map = am_map_set(&test_allocator, map, k3, am_make_value_of_uint(30)); assert(map != NULL);

    assert(am_map_delete(&test_allocator, map, k2) == 1);
    assert(am_map_size(&test_allocator, map) == 2);
    assert(am_map_contains(&test_allocator, map, k2) == 0);
    assert(am_value_equal(am_map_get(&test_allocator, map, k2), AM_VALUE_NULL));

    am_value_t k_missing = am_make_value_of_uint(999);
    assert(am_map_delete(&test_allocator, map, k_missing) == 0);

    assert(am_map_contains(&test_allocator, map, k1) == 1);
    assert(am_map_contains(&test_allocator, map, k3) == 1);
    assert(am_value_equal(am_map_get(&test_allocator, map, k1), am_make_value_of_uint(10)));
    assert(am_value_equal(am_map_get(&test_allocator, map, k3), am_make_value_of_uint(30)));

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_resize_and_rehash(void) {
    printf("test_resize_and_rehash ... ");

    am_map_t *map = am_map_create(&test_allocator, 4);
    assert(am_map_capacity(&test_allocator, map) == 8);

    const int N = 200;
    for (int i = 0; i < N; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)(1000 + i));
        am_value_t v = am_make_value_of_uint((uint32_t)i);
        map = am_map_set(&test_allocator, map, k, v);
        assert(map != NULL);
    }

    assert(am_map_size(&test_allocator, map) == (uint32_t)N);
    assert(am_map_capacity(&test_allocator, map) >= (uint32_t)N);

    for (int i = 0; i < N; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)(1000 + i));
        am_value_t expected = am_make_value_of_uint((uint32_t)i);
        assert(am_map_contains(&test_allocator, map, k) == 1);
        assert(am_value_equal(am_map_get(&test_allocator, map, k), expected));
    }

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_iter_and_keys(void) {
    printf("test_iter_and_keys ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);

    const int N = 10;
    for (int i = 0; i < N; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t v = am_make_value_of_uint((uint32_t)(i * i));
        map = am_map_set(&test_allocator, map, k, v);
        assert(map != NULL);
    }

    reset_iter_stats();
    am_map_iter(&test_allocator, map, sum_value_callback, NULL);
    assert(g_iter_count == N);
    int expected_sum = 0;
    for (int i = 0; i < N; i++) expected_sum += i * i;
    assert(g_iter_sum == expected_sum);

    am_value_t *keys = am_map_keys(&test_allocator, map);
    assert(keys != NULL);
    int key_sum = 0;
    for (int i = 0; i < N; i++) {
        assert(am_value_is_uint(keys[i]));
        key_sum += (int)am_value_to_uint(keys[i]);
    }
    assert(key_sum == (N - 1) * N / 2);
    free(keys);

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_pointer_value_ownership(void) {
    printf("test_pointer_value_ownership ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);

    void *payload1 = am_malloc(&test_allocator, sizeof(am_value_t));
    void *payload2 = am_malloc(&test_allocator, sizeof(am_value_t));

    am_value_t k = am_make_value_of_symbol(123);
    am_value_t v1 = am_make_value_of_ptr((am_object_t *)payload1);
    am_value_t v2 = am_make_value_of_ptr((am_object_t *)payload2);

    map = am_map_set(&test_allocator, map, k, v1);
    assert(map != NULL);
    assert(am_value_equal(am_map_get(&test_allocator, map, k), v1));

    map = am_map_set(&test_allocator, map, k, v2);
    assert(map != NULL);
    assert(am_value_equal(am_map_get(&test_allocator, map, k), v2));

    assert(am_map_delete(&test_allocator, map, k) == 1);
    assert(am_map_contains(&test_allocator, map, k) == 0);

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_clear_and_destroy(void) {
    printf("test_clear_and_destroy ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);

    for (int i = 0; i < 50; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t v = am_make_value_of_uint((uint32_t)(i + 1));
        map = am_map_set(&test_allocator, map, k, v);
        assert(map != NULL);
    }

    uint32_t capacity_before = am_map_capacity(&test_allocator, map);
    assert(capacity_before >= 8);

    assert(am_map_clear(&test_allocator, map) == 0);
    assert(am_map_size(&test_allocator, map) == 0);
    assert(am_map_capacity(&test_allocator, map) == capacity_before);

    for (int i = 0; i < 50; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        assert(am_map_contains(&test_allocator, map, k) == 0);
    }

    assert(am_map_destroy(&test_allocator, map) == 0);
    printf("OK\n");
}

static void test_reserved_keys_rejected(void) {
    printf("test_reserved_keys_rejected ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);

    am_value_t some_value = am_make_value_of_uint(1);
    assert(am_map_set(&test_allocator, map, AM_MAP_KEY_EMPTY, some_value) == NULL);
    assert(am_map_set(&test_allocator, map, AM_MAP_KEY_TOMBSTONE, some_value) == NULL);
    assert(am_map_size(&test_allocator, map) == 0);

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_set_stable_basic(void) {
    printf("test_set_stable_basic ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);
    assert(am_map_capacity(&test_allocator, map) == 8);

    for (int i = 0; i < 8; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t v = am_make_value_of_uint((uint32_t)(i * 10));
        assert(am_map_set_stable(&test_allocator, map, k, v) == 0);
    }
    assert(am_map_size(&test_allocator, map) == 8);
    assert(am_map_capacity(&test_allocator, map) == 8);

    for (int i = 0; i < 8; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t expected = am_make_value_of_uint((uint32_t)(i * 10));
        assert(am_value_equal(am_map_get(&test_allocator, map, k), expected));
    }

    am_value_t k0 = am_make_value_of_uint(0);
    assert(am_map_set_stable(&test_allocator, map, k0, am_make_value_of_uint(999)) == 0);
    assert(am_value_equal(am_map_get(&test_allocator, map, k0), am_make_value_of_uint(999)));
    assert(am_map_size(&test_allocator, map) == 8);

    am_value_t k_new = am_make_value_of_uint(100);
    assert(am_map_set_stable(&test_allocator, map, k_new, am_make_value_of_uint(1)) == -1);
    assert(am_map_size(&test_allocator, map) == 8);

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_set_stable_tombstone_reuse(void) {
    printf("test_set_stable_tombstone_reuse ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);

    for (int i = 0; i < 6; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t v = am_make_value_of_uint((uint32_t)i);
        assert(am_map_set_stable(&test_allocator, map, k, v) == 0);
    }
    assert(am_map_size(&test_allocator, map) == 6);

    assert(am_map_delete(&test_allocator, map, am_make_value_of_uint(2)) == 1);
    assert(am_map_delete(&test_allocator, map, am_make_value_of_uint(4)) == 1);
    assert(am_map_size(&test_allocator, map) == 4);

    assert(am_map_set_stable(&test_allocator, map, am_make_value_of_uint(20), am_make_value_of_uint(200)) == 0);
    assert(am_map_set_stable(&test_allocator, map, am_make_value_of_uint(21), am_make_value_of_uint(201)) == 0);
    assert(am_map_size(&test_allocator, map) == 6);
    assert(am_map_capacity(&test_allocator, map) == 8);

    assert(am_value_equal(am_map_get(&test_allocator, map, am_make_value_of_uint(0)),  am_make_value_of_uint(0)));
    assert(am_value_equal(am_map_get(&test_allocator, map, am_make_value_of_uint(20)), am_make_value_of_uint(200)));
    assert(am_value_equal(am_map_get(&test_allocator, map, am_make_value_of_uint(21)), am_make_value_of_uint(201)));

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_set_stable_pointer_ownership(void) {
    printf("test_set_stable_pointer_ownership ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);

    void *payload1 = am_malloc(&test_allocator, sizeof(am_value_t));
    void *payload2 = am_malloc(&test_allocator, sizeof(am_value_t));

    am_value_t k = am_make_value_of_symbol(77);
    assert(am_map_set_stable(&test_allocator, map, k, am_make_value_of_ptr((am_object_t *)payload1)) == 0);

    assert(am_map_set_stable(&test_allocator, map, k, am_make_value_of_ptr((am_object_t *)payload2)) == 0);

    assert(am_map_delete(&test_allocator, map, k) == 1);

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_set_stable_no_resize(void) {
    printf("test_set_stable_no_resize ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);

    for (int i = 0; i < 7; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t v = am_make_value_of_uint((uint32_t)i);
        assert(am_map_set_stable(&test_allocator, map, k, v) == 0);
    }

    assert(am_map_size(&test_allocator, map) == 7);
    assert(am_map_capacity(&test_allocator, map) == 8);

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

// ===============================================================================
// 入口
// ===============================================================================

int main(void) {
    printf("Running map tests...\n");

    test_create_and_basic_properties();
    test_set_get_contains_uint();
    test_different_value_types();
    test_delete_and_tombstones();
    test_resize_and_rehash();
    test_iter_and_keys();
    test_pointer_value_ownership();
    test_clear_and_destroy();
    test_reserved_keys_rejected();
    test_set_stable_basic();
    test_set_stable_tombstone_reuse();
    test_set_stable_pointer_ownership();
    test_set_stable_no_resize();

    printf("All map tests passed.\n");
    return 0;
}
