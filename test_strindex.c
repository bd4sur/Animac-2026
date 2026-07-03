#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>

#include "object.h"
#include "allocator.h"
#include "wstring.h"

// ===============================================================================
// 测试用简单分配器（基于系统 malloc/free）
// ===============================================================================

static void *test_malloc(void *state, size_t size) {
    (void)state;
    return malloc(size);
}

static void *test_calloc(void *state, size_t size) {
    (void)state;
    return calloc(1, size);
}

static void *test_realloc(void *state, void *ptr, size_t size) {
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

static am_allocator_t test_allocator = {
    &test_allocator_vtable,
    NULL
};

static am_allocator_t *g_alloc = &test_allocator;

// ===============================================================================
// 辅助宏
// ===============================================================================

#define FAIL_IF(cond, msg) do { \
    if (cond) { \
        fprintf(stderr, "FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
        return -1; \
    } \
} while (0)

#define RUN_TEST(name) do { \
    printf("Running %s ...\n", #name); \
    if (name() != 0) { \
        fprintf(stderr, "TEST FAILED: %s\n", #name); \
        return 1; \
    } \
} while (0)

// ===============================================================================
// 测试用例
// ===============================================================================

static int test_create_destroy(void) {
    am_strindex_t *si = am_strindex_create(g_alloc, 4);
    FAIL_IF(si == NULL, "create failed");
    FAIL_IF(si->base.type != AM_OBJECT_TYPE_STRINDEX, "wrong object type");
    FAIL_IF(am_strindex_length(g_alloc, si) != 0, "length should be 0");
    FAIL_IF(am_strindex_capacity(g_alloc, si) < 4, "capacity too small");
    FAIL_IF((am_strindex_capacity(g_alloc, si) & (am_strindex_capacity(g_alloc, si) - 1)) != 0,
            "capacity should be power of 2");

    FAIL_IF(am_strindex_destroy(g_alloc, si) != 0, "destroy failed");
    FAIL_IF(am_strindex_destroy(g_alloc, NULL) != 0, "destroy NULL should succeed");
    return 0;
}

static int test_set_and_get(void) {
    am_strindex_t *si = am_strindex_create(g_alloc, 8);
    FAIL_IF(si == NULL, "create failed");

    am_value_t v1 = am_make_value_of_handle((am_handle_t)0x123);
    am_value_t v2 = am_make_value_of_handle((am_handle_t)0x456);
    am_value_t v3 = am_make_value_of_handle((am_handle_t)0x789);

    si = am_strindex_set(g_alloc, si, L"hello", v1);
    FAIL_IF(si == NULL, "set hello failed");
    si = am_strindex_set(g_alloc, si, L"world", v2);
    FAIL_IF(si == NULL, "set world failed");
    si = am_strindex_set(g_alloc, si, L"foo", v3);
    FAIL_IF(si == NULL, "set foo failed");

    FAIL_IF(am_strindex_length(g_alloc, si) != 3, "length should be 3");

    am_value_t values[4];
    size_t n;

    n = am_strindex_get_all(g_alloc, si, L"hello", values, 4);
    FAIL_IF(n != 1, "hello should have 1 match");
    FAIL_IF(values[0] != v1, "hello value mismatch");

    n = am_strindex_get_all(g_alloc, si, L"world", values, 4);
    FAIL_IF(n != 1, "world should have 1 match");
    FAIL_IF(values[0] != v2, "world value mismatch");

    n = am_strindex_get_all(g_alloc, si, L"notexist", values, 4);
    FAIL_IF(n != 0, "notexist should have 0 matches");

    // values buffer too small: should count but not overflow
    n = am_strindex_get_all(g_alloc, si, L"hello", values, 0);
    FAIL_IF(n != 1, "count mode should return 1");

    FAIL_IF(am_strindex_destroy(g_alloc, si) != 0, "destroy failed");
    return 0;
}

static int test_multiple_values_same_hash(void) {
    // 由于无法方便地构造 FNV-1a 冲突，这里用同一个字符串多次 set，
    // 验证多值语义：同一个 hash 下可以挂多个 value。
    am_strindex_t *si = am_strindex_create(g_alloc, 8);
    FAIL_IF(si == NULL, "create failed");

    am_value_t v1 = am_make_value_of_handle((am_handle_t)0x111);
    am_value_t v2 = am_make_value_of_handle((am_handle_t)0x222);
    am_value_t v3 = am_make_value_of_handle((am_handle_t)0x333);

    si = am_strindex_set(g_alloc, si, L"same", v1);
    FAIL_IF(si == NULL, "set same v1 failed");
    si = am_strindex_set(g_alloc, si, L"same", v2);
    FAIL_IF(si == NULL, "set same v2 failed");
    si = am_strindex_set(g_alloc, si, L"same", v3);
    FAIL_IF(si == NULL, "set same v3 failed");

    FAIL_IF(am_strindex_length(g_alloc, si) != 3, "length should be 3");

    am_value_t values[3];
    size_t n = am_strindex_get_all(g_alloc, si, L"same", values, 3);
    FAIL_IF(n != 3, "same should have 3 matches");

    // 由于开放寻址插入顺序，value 顺序与插入顺序一致
    FAIL_IF(values[0] != v1, "first value mismatch");
    FAIL_IF(values[1] != v2, "second value mismatch");
    FAIL_IF(values[2] != v3, "third value mismatch");

    // buffer 容量不足
    am_value_t small[2];
    n = am_strindex_get_all(g_alloc, si, L"same", small, 2);
    FAIL_IF(n != 3, "count should still be 3");
    FAIL_IF(small[0] != v1, "small[0] mismatch");
    FAIL_IF(small[1] != v2, "small[1] mismatch");

    FAIL_IF(am_strindex_destroy(g_alloc, si) != 0, "destroy failed");
    return 0;
}

static int test_resize(void) {
    am_strindex_t *si = am_strindex_create(g_alloc, 8);
    FAIL_IF(si == NULL, "create failed");
    size_t initial_cap = am_strindex_capacity(g_alloc, si);

    // 插入足够多的条目触发扩容（负载因子 > 75%）
    for (int i = 0; i < 16; i++) {
        wchar_t buf[16];
        swprintf(buf, 16, L"key%d", i);
        am_value_t v = am_make_value_of_handle((am_handle_t)(i + 1));
        am_strindex_t *new_si = am_strindex_set(g_alloc, si, buf, v);
        FAIL_IF(new_si == NULL, "set failed during resize");
        si = new_si;
    }

    FAIL_IF(am_strindex_capacity(g_alloc, si) <= initial_cap,
            "capacity should have grown");
    FAIL_IF(am_strindex_length(g_alloc, si) != 16, "length should be 16");

    // 验证所有条目仍可查询
    for (int i = 0; i < 16; i++) {
        wchar_t buf[16];
        swprintf(buf, 16, L"key%d", i);
        am_value_t expected = am_make_value_of_handle((am_handle_t)(i + 1));
        am_value_t value;
        size_t n = am_strindex_get_all(g_alloc, si, buf, &value, 1);
        FAIL_IF(n != 1, "missing key after resize");
        FAIL_IF(value != expected, "value mismatch after resize");
    }

    FAIL_IF(am_strindex_destroy(g_alloc, si) != 0, "destroy failed");
    return 0;
}

static int test_delete(void) {
    am_strindex_t *si = am_strindex_create(g_alloc, 8);
    FAIL_IF(si == NULL, "create failed");

    am_value_t v1 = am_make_value_of_handle((am_handle_t)0xAAA);
    am_value_t v2 = am_make_value_of_handle((am_handle_t)0xBBB);
    am_value_t v3 = am_make_value_of_handle((am_handle_t)0xCCC);

    si = am_strindex_set(g_alloc, si, L"alpha", v1);
    FAIL_IF(si == NULL, "set alpha failed");
    si = am_strindex_set(g_alloc, si, L"beta", v2);
    FAIL_IF(si == NULL, "set beta failed");
    si = am_strindex_set(g_alloc, si, L"gamma", v3);
    FAIL_IF(si == NULL, "set gamma failed");

    // 删除 v2
    FAIL_IF(am_strindex_delete(g_alloc, si, v2) != 0, "delete v2 failed");
    FAIL_IF(am_strindex_length(g_alloc, si) != 2, "length should be 2 after delete");

    am_value_t values[4];
    size_t n;

    n = am_strindex_get_all(g_alloc, si, L"alpha", values, 4);
    FAIL_IF(n != 1 || values[0] != v1, "alpha should remain");

    n = am_strindex_get_all(g_alloc, si, L"beta", values, 4);
    FAIL_IF(n != 0, "beta should be deleted");

    n = am_strindex_get_all(g_alloc, si, L"gamma", values, 4);
    FAIL_IF(n != 1 || values[0] != v3, "gamma should remain");

    // 删除不存在的 value
    am_value_t v4 = am_make_value_of_handle((am_handle_t)0xDDD);
    FAIL_IF(am_strindex_delete(g_alloc, si, v4) != -1, "delete nonexistent should fail");

    // 删除后重新插入，验证墓碑复用
    si = am_strindex_set(g_alloc, si, L"beta", v2);
    FAIL_IF(si == NULL, "re-set beta failed");
    FAIL_IF(am_strindex_length(g_alloc, si) != 3, "length should be 3 after reinsert");

    FAIL_IF(am_strindex_destroy(g_alloc, si) != 0, "destroy failed");
    return 0;
}

static int test_copy(void) {
    am_strindex_t *si = am_strindex_create(g_alloc, 8);
    FAIL_IF(si == NULL, "create failed");

    am_value_t v1 = am_make_value_of_handle((am_handle_t)0x1111);
    am_value_t v2 = am_make_value_of_handle((am_handle_t)0x2222);
    si = am_strindex_set(g_alloc, si, L"one", v1);
    FAIL_IF(si == NULL, "set one failed");
    si = am_strindex_set(g_alloc, si, L"two", v2);
    FAIL_IF(si == NULL, "set two failed");

    am_strindex_t *copy = am_strindex_copy(g_alloc, si);
    FAIL_IF(copy == NULL, "copy failed");
    FAIL_IF(copy == si, "copy should be a new object");
    FAIL_IF(am_strindex_length(g_alloc, copy) != am_strindex_length(g_alloc, si),
            "copy length mismatch");
    FAIL_IF(am_strindex_capacity(g_alloc, copy) != am_strindex_capacity(g_alloc, si),
            "copy capacity mismatch");

    am_value_t values[4];
    size_t n;
    n = am_strindex_get_all(g_alloc, copy, L"one", values, 4);
    FAIL_IF(n != 1 || values[0] != v1, "copy one mismatch");
    n = am_strindex_get_all(g_alloc, copy, L"two", values, 4);
    FAIL_IF(n != 1 || values[0] != v2, "copy two mismatch");

    FAIL_IF(am_strindex_destroy(g_alloc, si) != 0, "destroy original failed");
    FAIL_IF(am_strindex_destroy(g_alloc, copy) != 0, "destroy copy failed");
    return 0;
}

static int test_dump_load(void) {
    am_strindex_t *si = am_strindex_create(g_alloc, 8);
    FAIL_IF(si == NULL, "create failed");

    am_value_t v1 = am_make_value_of_handle((am_handle_t)0xABCD);
    am_value_t v2 = am_make_value_of_handle((am_handle_t)0xEF01);
    am_value_t v3 = am_make_value_of_handle((am_handle_t)0x2345);
    si = am_strindex_set(g_alloc, si, L"first", v1);
    FAIL_IF(si == NULL, "set first failed");
    si = am_strindex_set(g_alloc, si, L"second", v2);
    FAIL_IF(si == NULL, "set second failed");
    si = am_strindex_set(g_alloc, si, L"third", v3);
    FAIL_IF(si == NULL, "set third failed");

    // 先删除一个，制造墓碑，验证 dump 丢弃墓碑
    FAIL_IF(am_strindex_delete(g_alloc, si, v2) != 0, "delete failed");

    size_t dump_size = am_strindex_dump(g_alloc, si, NULL, 0);
    FAIL_IF(dump_size == SIZE_MAX, "dump size calculation failed");

    uint8_t *buffer = (uint8_t *)malloc(dump_size);
    FAIL_IF(buffer == NULL, "malloc buffer failed");

    size_t written = am_strindex_dump(g_alloc, si, buffer, 0);
    FAIL_IF(written != dump_size, "dump size mismatch");

    am_strindex_t *loaded = am_strindex_load(g_alloc, buffer, 0);
    FAIL_IF(loaded == NULL, "load failed");
    FAIL_IF(loaded->base.type != AM_OBJECT_TYPE_STRINDEX, "loaded type mismatch");
    FAIL_IF(am_strindex_length(g_alloc, loaded) != 2, "loaded length should be 2");

    am_value_t values[4];
    size_t n;
    n = am_strindex_get_all(g_alloc, loaded, L"first", values, 4);
    FAIL_IF(n != 1 || values[0] != v1, "loaded first mismatch");
    n = am_strindex_get_all(g_alloc, loaded, L"second", values, 4);
    FAIL_IF(n != 0, "deleted second should not be loaded");
    n = am_strindex_get_all(g_alloc, loaded, L"third", values, 4);
    FAIL_IF(n != 1 || values[0] != v3, "loaded third mismatch");

    free(buffer);
    FAIL_IF(am_strindex_destroy(g_alloc, si) != 0, "destroy original failed");
    FAIL_IF(am_strindex_destroy(g_alloc, loaded) != 0, "destroy loaded failed");
    return 0;
}

static int test_null_and_errors(void) {
    am_value_t values[4];

    FAIL_IF(am_strindex_create(NULL, 8) != NULL, "create with NULL alloc should fail");
    FAIL_IF(am_strindex_destroy(g_alloc, NULL) != 0, "destroy NULL should succeed");
    FAIL_IF(am_strindex_copy(g_alloc, NULL) != NULL, "copy NULL should fail");
    FAIL_IF(am_strindex_size(g_alloc, NULL) != SIZE_MAX, "size NULL should return SIZE_MAX");
    FAIL_IF(am_strindex_dump(g_alloc, NULL, NULL, 0) != SIZE_MAX, "dump NULL should return SIZE_MAX");
    FAIL_IF(am_strindex_load(g_alloc, NULL, 0) != NULL, "load NULL buffer should fail");
    FAIL_IF(am_strindex_length(g_alloc, NULL) != SIZE_MAX, "length NULL should return SIZE_MAX");
    FAIL_IF(am_strindex_capacity(g_alloc, NULL) != SIZE_MAX, "capacity NULL should return SIZE_MAX");

    am_strindex_t *si = am_strindex_create(g_alloc, 8);
    FAIL_IF(si == NULL, "create failed");

    am_value_t v = am_make_value_of_handle((am_handle_t)0x1);
    FAIL_IF(am_strindex_set(g_alloc, si, NULL, v) != NULL, "set NULL str should fail");
    FAIL_IF(am_strindex_set(NULL, si, L"x", v) != NULL, "set NULL alloc should fail");
    FAIL_IF(am_strindex_get_all(g_alloc, si, NULL, values, 4) != SIZE_MAX,
            "get_all NULL str should return SIZE_MAX");
    FAIL_IF(am_strindex_get_all(NULL, si, L"x", values, 4) != SIZE_MAX,
            "get_all NULL alloc should return SIZE_MAX");
    FAIL_IF(am_strindex_get_all(g_alloc, NULL, L"x", values, 4) != SIZE_MAX,
            "get_all NULL obj should return SIZE_MAX");
    FAIL_IF(am_strindex_delete(NULL, si, v) != -1, "delete NULL alloc should fail");
    FAIL_IF(am_strindex_delete(g_alloc, NULL, v) != -1, "delete NULL obj should fail");

    FAIL_IF(am_strindex_destroy(g_alloc, si) != 0, "destroy failed");
    return 0;
}

// ===============================================================================
// 主函数
// ===============================================================================

int main(void) {
    printf("=== am_strindex_t unit tests ===\n");

    RUN_TEST(test_create_destroy);
    RUN_TEST(test_set_and_get);
    RUN_TEST(test_multiple_values_same_hash);
    RUN_TEST(test_resize);
    RUN_TEST(test_delete);
    RUN_TEST(test_copy);
    RUN_TEST(test_dump_load);
    RUN_TEST(test_null_and_errors);

    printf("=== all tests passed ===\n");
    return 0;
}
