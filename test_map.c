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
// 基础设施：基于内存池的简单测试分配器（bump allocator）
// ===============================================================================

#define TEST_POOL_SIZE 2048

typedef struct test_allocator_state_t {
    uint8_t *base;      // 内存池基地址
    size_t offset;      // 下一个可分配位置的偏移量
    size_t capacity;    // 内存池总容量
} test_allocator_state_t;

static test_allocator_state_t test_allocator_state;

static void* test_malloc(void *state, size_t size);
static void* test_calloc(void *state, size_t size);
static void* test_realloc(void *state, void *ptr, size_t size);
static void  test_free(void *state, void *ptr);
static void  test_destroy(void *state);

static size_t test_align_size(size_t size) {
    const size_t align = sizeof(void*);
    return (size + align - 1) & ~(align - 1);
}

static void test_print_progress_bar(void) {
    test_allocator_state_t *s = &test_allocator_state;
    const int width = 50;
    int used_chars = (int)((s->offset * width + s->capacity - 1) / s->capacity);
    if (used_chars > width) used_chars = width;

    printf("[BAR]  [");
    for (int i = 0; i < used_chars; i++) putchar('#');
    for (int i = used_chars; i < width; i++) putchar('-');
    printf("]\n");
}

static void test_scan_pool(void) {
    test_allocator_state_t *s = &test_allocator_state;
    printf("[SCAN] ");
    size_t i = 0;
    int first = 1;
    while (i < s->capacity) {
        bool used = (i < s->offset);
        size_t start = i;
        while (i < s->capacity && (i < s->offset) == used) {
            i++;
        }
        if (!first) printf(", ");
        first = 0;
        printf("start=%-4zu %4zu bytes %s", start, i - start, used ? "used" : "free");
    }
    printf("\n");
}

static void test_print_layout(const char *op, size_t size, void *ptr) {
    test_allocator_state_t *s = &test_allocator_state;

    printf("[POOL] %-10s", op);
    if (size > 0) printf(" size=%4zu ptr=%p", size, ptr);
    printf(" offset=%4zu/%4zu\n", s->offset, s->capacity);

    test_print_progress_bar();
    test_scan_pool();
}

static void* test_malloc(void *state, size_t size) {
    test_allocator_state_t *s = (test_allocator_state_t *)state;
    if (size == 0) {
        test_print_layout("malloc", 0, NULL);
        return NULL;
    }
    size_t aligned_size = test_align_size(size);
    if (s->offset + aligned_size > s->capacity) {
        test_print_layout("malloc-FAIL", size, NULL);
        return NULL;
    }
    void *p = s->base + s->offset;
    s->offset += aligned_size;
    test_print_layout("malloc", size, p);
    return p;
}

static void* test_calloc(void *state, size_t size) {
    void *p = test_malloc(state, size);
    if (p) memset(p, 0, size);
    // layout 已在 test_malloc 中打印
    return p;
}

static void* test_realloc(void *state, void *ptr, size_t size) {
    if (ptr == NULL) return test_malloc(state, size);
    // Bump allocator 不记录单个对象尺寸，无法原地扩展。
    // 简单实现为分配新块并按新尺寸拷贝；map.h 的实现中不会调用 am_realloc。
    void *new_ptr = test_malloc(state, size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, size);
    }
    // layout 已在 test_malloc 中打印
    return new_ptr;
}

static void test_free(void *state, void *ptr) {
    (void)state;
    // Bump allocator：释放为 no-op，内存池在 destroy 时统一释放。
    test_print_layout("free", 0, ptr);
}

static void test_destroy(void *state) {
    test_allocator_state_t *s = (test_allocator_state_t *)state;
    if (s->base) {
        free(s->base);
        s->base = NULL;
    }
    s->offset = 0;
    s->capacity = 0;
}

static void test_allocator_init(void) {
    test_allocator_state.base = (uint8_t *)malloc(TEST_POOL_SIZE);
    test_allocator_state.offset = 0;
    test_allocator_state.capacity = TEST_POOL_SIZE;
}

static void test_allocator_reset(void) {
    test_allocator_state.offset = 0;
}

static const am_allocator_vtable_t test_allocator_vtable = {
    test_malloc,
    test_calloc,
    test_realloc,
    test_free,
    test_destroy
};

static am_allocator_t test_allocator = { &test_allocator_vtable, &test_allocator_state };

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
    assert(am_map_length(&test_allocator, map) == 0);

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
    assert(am_map_length(&test_allocator, map) == 1);
    assert(am_map_contains(&test_allocator, map, k1) == 1);
    assert(am_value_equal(am_map_get(&test_allocator, map, k1), v1));

    am_value_t k2 = am_make_value_of_uint(43);
    assert(am_map_contains(&test_allocator, map, k2) == 0);
    assert(am_value_equal(am_map_get(&test_allocator, map, k2), AM_VALUE_NULL));

    am_value_t v1_new = am_make_value_of_uint(200);
    map = am_map_set(&test_allocator, map, k1, v1_new);
    assert(map != NULL);
    assert(am_map_length(&test_allocator, map) == 1);
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

    assert(am_map_length(&test_allocator, map) == 4);
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
    assert(am_map_length(&test_allocator, map) == 2);
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

    const int N = 20;
    for (int i = 0; i < N; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)(1000 + i));
        am_value_t v = am_make_value_of_uint((uint32_t)i);
        map = am_map_set(&test_allocator, map, k, v);
        assert(map != NULL);
    }

    assert(am_map_length(&test_allocator, map) == (uint32_t)N);
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

    for (int i = 0; i < 20; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t v = am_make_value_of_uint((uint32_t)(i + 1));
        map = am_map_set(&test_allocator, map, k, v);
        assert(map != NULL);
    }

    uint32_t capacity_before = am_map_capacity(&test_allocator, map);
    assert(capacity_before >= 8);

    assert(am_map_clear(&test_allocator, map) == 0);
    assert(am_map_length(&test_allocator, map) == 0);
    assert(am_map_capacity(&test_allocator, map) == capacity_before);

    for (int i = 0; i < 20; i++) {
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
    assert(am_map_length(&test_allocator, map) == 0);

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
    assert(am_map_length(&test_allocator, map) == 8);
    assert(am_map_capacity(&test_allocator, map) == 8);

    for (int i = 0; i < 8; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t expected = am_make_value_of_uint((uint32_t)(i * 10));
        assert(am_value_equal(am_map_get(&test_allocator, map, k), expected));
    }

    am_value_t k0 = am_make_value_of_uint(0);
    assert(am_map_set_stable(&test_allocator, map, k0, am_make_value_of_uint(999)) == 0);
    assert(am_value_equal(am_map_get(&test_allocator, map, k0), am_make_value_of_uint(999)));
    assert(am_map_length(&test_allocator, map) == 8);

    am_value_t k_new = am_make_value_of_uint(100);
    assert(am_map_set_stable(&test_allocator, map, k_new, am_make_value_of_uint(1)) == -1);
    assert(am_map_length(&test_allocator, map) == 8);

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
    assert(am_map_length(&test_allocator, map) == 6);

    assert(am_map_delete(&test_allocator, map, am_make_value_of_uint(2)) == 1);
    assert(am_map_delete(&test_allocator, map, am_make_value_of_uint(4)) == 1);
    assert(am_map_length(&test_allocator, map) == 4);

    assert(am_map_set_stable(&test_allocator, map, am_make_value_of_uint(20), am_make_value_of_uint(200)) == 0);
    assert(am_map_set_stable(&test_allocator, map, am_make_value_of_uint(21), am_make_value_of_uint(201)) == 0);
    assert(am_map_length(&test_allocator, map) == 6);
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

    assert(am_map_length(&test_allocator, map) == 7);
    assert(am_map_capacity(&test_allocator, map) == 8);

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_dump_basic(void) {
    printf("test_dump_basic ... ");

    // 空 map 转储
    am_map_t *map = am_map_create(&test_allocator, 8);
    size_t size = 0;
    uint8_t *dump = am_map_dump(&test_allocator, map, &size);
    assert(dump != NULL);
    assert(size == sizeof(am_map_t));
    am_map_t *d = (am_map_t *)dump;
    assert(d->length == 0);
    assert(d->capacity == 0);
    assert(d->tombstones == 0);
    free(dump);
    am_map_destroy(&test_allocator, map);

    // 非空 map 转储
    map = am_map_create(&test_allocator, 8);
    for (int i = 0; i < 5; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t v = am_make_value_of_uint((uint32_t)(i * 10));
        map = am_map_set(&test_allocator, map, k, v);
        assert(map != NULL);
    }
    assert(am_map_length(&test_allocator, map) == 5);
    assert(am_map_capacity(&test_allocator, map) == 8);

    dump = am_map_dump(&test_allocator, map, &size);
    assert(dump != NULL);
    assert(size == sizeof(am_map_t) + 5 * sizeof(am_map_entry_t));
    d = (am_map_t *)dump;
    assert(d->length == 5);
    assert(d->capacity == 5);
    assert(d->tombstones == 0);

    for (int i = 0; i < 5; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t expected = am_make_value_of_uint((uint32_t)(i * 10));
        int found = 0;
        for (size_t j = 0; j < d->length; j++) {
            if (am_value_equal(d->slots[j].key, k) && am_value_equal(d->slots[j].value, expected)) {
                found = 1;
                break;
            }
        }
        assert(found);
    }
    free(dump);

    // 带墓碑的 map 转储：墓碑应被丢弃
    am_value_t k2 = am_make_value_of_uint(2);
    assert(am_map_delete(&test_allocator, map, k2) == 1);
    assert(am_map_length(&test_allocator, map) == 4);

    dump = am_map_dump(&test_allocator, map, &size);
    assert(dump != NULL);
    assert(size == sizeof(am_map_t) + 4 * sizeof(am_map_entry_t));
    d = (am_map_t *)dump;
    assert(d->length == 4);
    assert(d->capacity == 4);
    assert(d->tombstones == 0);
    free(dump);

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_dump_vs_copy(void) {
    printf("test_dump_vs_copy ... ");

    am_map_t *map = am_map_create(&test_allocator, 8);
    for (int i = 0; i < 5; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t v = am_make_value_of_uint((uint32_t)(i * 10));
        map = am_map_set(&test_allocator, map, k, v);
        assert(map != NULL);
    }
    am_value_t k2 = am_make_value_of_uint(2);
    assert(am_map_delete(&test_allocator, map, k2) == 1);

    // copy 完整保留源对象：capacity、length、tombstones、所有槽位均不变
    am_map_t *copy = am_map_copy(&test_allocator, map);
    assert(copy != NULL);
    assert(copy->capacity == map->capacity);
    assert(copy->tombstones == map->tombstones);
    assert(copy->length == map->length);

    // dump 压缩对象：capacity 与 length 一致，丢弃墓碑和空闲槽位
    size_t dump_size = 0;
    uint8_t *dump = am_map_dump(&test_allocator, map, &dump_size);
    assert(dump != NULL);
    am_map_t *d = (am_map_t *)dump;
    assert(d->capacity == d->length);
    assert(d->tombstones == 0);
    assert(d->length == 4);
    assert(dump_size == sizeof(am_map_t) + d->length * sizeof(am_map_entry_t));

    free(dump);
    am_map_destroy(&test_allocator, copy);
    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

// ===============================================================================
// 入口
// ===============================================================================

#define RUN_TEST(t) do { test_allocator_reset(); t(); } while (0)

int main(void) {
    printf("Running map tests...\n");
    test_allocator_init();

    RUN_TEST(test_create_and_basic_properties);
    RUN_TEST(test_set_get_contains_uint);
    RUN_TEST(test_different_value_types);
    RUN_TEST(test_delete_and_tombstones);
    RUN_TEST(test_resize_and_rehash);
    RUN_TEST(test_iter_and_keys);
    RUN_TEST(test_pointer_value_ownership);
    RUN_TEST(test_clear_and_destroy);
    RUN_TEST(test_reserved_keys_rejected);
    RUN_TEST(test_set_stable_basic);
    RUN_TEST(test_set_stable_tombstone_reuse);
    RUN_TEST(test_set_stable_pointer_ownership);
    RUN_TEST(test_set_stable_no_resize);
    RUN_TEST(test_dump_basic);
    RUN_TEST(test_dump_vs_copy);

    test_destroy(test_allocator.state);
    printf("All map tests passed.\n");
    return 0;
}
