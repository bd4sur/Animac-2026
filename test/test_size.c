#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wchar.h>

// object.h 中部分内联函数存在未使用参数，属于已有代码的警告；
// 仅在本测试文件中临时忽略，不影响各头文件的实现。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "object.h"
#include "closure.h"
#include "continuation.h"
#include "list.h"
#include "map.h"
#include "wstring.h"
#pragma GCC diagnostic pop

// ===============================================================================
// 基础设施：基于系统 malloc 并记录每次分配请求字节数的测试分配器
// ===============================================================================

#define MAX_ALLOC_RECORDS 256

static struct {
    void *ptr;
    size_t size;
} g_alloc_records[MAX_ALLOC_RECORDS];
static size_t g_alloc_count = 0;

static void record_alloc(void *ptr, size_t size) {
    if (!ptr) return;
    assert(g_alloc_count < MAX_ALLOC_RECORDS);
    g_alloc_records[g_alloc_count].ptr = ptr;
    g_alloc_records[g_alloc_count].size = size;
    g_alloc_count++;
}

static void record_free(void *ptr) {
    if (!ptr) return;
    for (size_t i = 0; i < g_alloc_count; i++) {
        if (g_alloc_records[i].ptr == ptr) {
            g_alloc_records[i] = g_alloc_records[g_alloc_count - 1];
            g_alloc_count--;
            return;
        }
    }
}

static size_t lookup_size(void *ptr) {
    if (!ptr) return SIZE_MAX;
    for (size_t i = 0; i < g_alloc_count; i++) {
        if (g_alloc_records[i].ptr == ptr) {
            return g_alloc_records[i].size;
        }
    }
    return SIZE_MAX;
}

static void* test_malloc(void *state, size_t size) {
    (void)state;
    void *p = malloc(size);
    record_alloc(p, size);
    return p;
}

static void* test_calloc(void *state, size_t size) {
    (void)state;
    void *p = calloc(1, size);
    record_alloc(p, size);
    return p;
}

static void* test_realloc(void *state, void *ptr, size_t size) {
    (void)state;
    record_free(ptr);
    void *p = realloc(ptr, size);
    record_alloc(p, size);
    return p;
}

static void test_free(void *state, void *ptr) {
    (void)state;
    record_free(ptr);
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

static void test_closure_size(void) {
    printf("test_closure_size ... ");

    am_obj_closure_t *closure = am_closure_create(&test_allocator, 1, 2, 0);
    assert(closure != NULL);
    assert(closure->capacity == 16);

    size_t expected = sizeof(am_obj_closure_t) + closure->capacity * sizeof(am_binding_t);
    assert(am_closure_size(&test_allocator, closure) == expected);
    assert(am_closure_size(&test_allocator, closure) == lookup_size(closure));

    // 触发扩容
    for (int i = 0; i < 20; i++) {
        closure = am_closure_init_bound_var(
            &test_allocator, closure, (am_varid_t)i,
            am_make_value_of_uint((uint32_t)i));
        assert(closure != NULL);
    }
    assert(am_closure_size(&test_allocator, closure) == lookup_size(closure));
    assert(am_closure_size(&test_allocator, closure) ==
           sizeof(am_obj_closure_t) + closure->capacity * sizeof(am_binding_t));

    // size 应包含分配容量，而非仅 length
    assert(am_closure_size(&test_allocator, closure) >
           sizeof(am_obj_closure_t) + closure->length * sizeof(am_binding_t));

    assert(am_closure_size(&test_allocator, NULL) == SIZE_MAX);

    am_closure_destroy(&test_allocator, closure);
    printf("OK\n");
}

static void test_continuation_size(void) {
    printf("test_continuation_size ... ");

    am_value_t opstack[] = {
        am_make_value_of_uint(10),
        am_make_value_of_uint(20),
        am_make_value_of_uint(30)
    };
    am_value_t fstack[] = {
        am_make_value_of_uint(100),
        am_make_value_of_uint(200)
    };

    am_continuation_t *cont = am_continuation_create(
        &test_allocator, 42, 7, opstack, 3, fstack, 2);
    assert(cont != NULL);

    size_t expected = sizeof(am_continuation_t) + cont->length * sizeof(am_value_t);
    assert(am_continuation_size(&test_allocator, cont) == expected);
    assert(am_continuation_size(&test_allocator, cont) == lookup_size(cont));

    // 空续体
    am_continuation_t *empty = am_continuation_create(
        &test_allocator, 0, 0, NULL, 0, NULL, 0);
    assert(empty != NULL);
    assert(am_continuation_size(&test_allocator, empty) == sizeof(am_continuation_t));
    assert(am_continuation_size(&test_allocator, empty) == lookup_size(empty));

    assert(am_continuation_size(&test_allocator, NULL) == SIZE_MAX);

    am_continuation_destroy(&test_allocator, cont);
    am_continuation_destroy(&test_allocator, empty);
    printf("OK\n");
}

static void test_list_size(void) {
    printf("test_list_size ... ");

    am_list_t *lst = am_list_create(&test_allocator, 4, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    assert(lst != NULL);
    assert(lst->capacity == 4);

    size_t expected = sizeof(am_list_t) + lst->capacity * sizeof(am_value_t);
    assert(am_list_size(&test_allocator, lst) == expected);
    assert(am_list_size(&test_allocator, lst) == lookup_size(lst));

    // 触发扩容
    for (int i = 0; i < 10; i++) {
        lst = am_list_push(&test_allocator, lst, am_make_value_of_uint((uint32_t)i));
        assert(lst != NULL);
    }
    assert(am_list_size(&test_allocator, lst) == lookup_size(lst));
    assert(am_list_size(&test_allocator, lst) ==
           sizeof(am_list_t) + lst->capacity * sizeof(am_value_t));

    // size 应包含分配容量，而非仅 length
    assert(am_list_size(&test_allocator, lst) >
           sizeof(am_list_t) + lst->length * sizeof(am_value_t));

    assert(am_list_size(&test_allocator, NULL) == SIZE_MAX);

    am_list_destroy(&test_allocator, lst);
    printf("OK\n");
}

static void test_map_size(void) {
    printf("test_map_size ... ");

    am_map_t *map = am_map_create(&test_allocator, 5);
    assert(map != NULL);
    assert(map->capacity == 8);

    size_t expected = sizeof(am_map_t) + map->capacity * sizeof(am_map_entry_t);
    assert(am_map_size(&test_allocator, map) == expected);
    assert(am_map_size(&test_allocator, map) == lookup_size(map));

    // 触发扩容
    for (int i = 0; i < 20; i++) {
        map = am_map_set(
            &test_allocator, map,
            am_make_value_of_uint((uint32_t)(1000 + i)),
            am_make_value_of_uint((uint32_t)i));
        assert(map != NULL);
    }
    assert(am_map_size(&test_allocator, map) == lookup_size(map));
    assert(am_map_size(&test_allocator, map) ==
           sizeof(am_map_t) + map->capacity * sizeof(am_map_entry_t));

    // size 应包含物理槽位容量，而非仅有效键值对数量
    assert(am_map_size(&test_allocator, map) >
           sizeof(am_map_t) + map->length * sizeof(am_map_entry_t));

    assert(am_map_size(&test_allocator, NULL) == SIZE_MAX);

    am_map_destroy(&test_allocator, map);
    printf("OK\n");
}

static void test_wstring_size(void) {
    printf("test_wstring_size ... ");

    wchar_t text[] = L"hello";
    am_wstring_t *ws = am_wstring_create(&test_allocator, text, 5);
    assert(ws != NULL);

    size_t expected = sizeof(am_wstring_t) + ws->length * sizeof(am_value_t);
    assert(am_wstring_size(&test_allocator, ws) == expected);
    assert(am_wstring_size(&test_allocator, ws) == lookup_size(ws));

    am_wstring_t *empty = am_wstring_create(&test_allocator, L"", 0);
    assert(empty != NULL);
    assert(am_wstring_size(&test_allocator, empty) == sizeof(am_wstring_t));
    assert(am_wstring_size(&test_allocator, empty) == lookup_size(empty));

    assert(am_wstring_size(&test_allocator, NULL) == SIZE_MAX);

    am_wstring_destroy(&test_allocator, ws);
    am_wstring_destroy(&test_allocator, empty);
    printf("OK\n");
}

// ===============================================================================
// 主函数
// ===============================================================================

int main(void) {
    printf("Running object size tests...\n");

    test_closure_size();
    test_continuation_size();
    test_list_size();
    test_map_size();
    test_wstring_size();

    printf("All object size tests passed.\n");
    return 0;
}
