#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// gcc -o test_map_copy test_map_copy.c -Wall -Wextra

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "map.h"
#pragma GCC diagnostic pop

static void* test_malloc(void *state, size_t size) { (void)state; return malloc(size); }
static void* test_calloc(void *state, size_t size) { (void)state; return calloc(1, size); }
static void* test_realloc(void *state, void *ptr, size_t size) { (void)state; return realloc(ptr, size); }
static void  test_free(void *state, void *ptr) { (void)state; free(ptr); }
static void  test_destroy(void *state) { (void)state; }

static const am_allocator_vtable_t test_allocator_vtable = {
    test_malloc, test_calloc, test_realloc, test_free, test_destroy
};
static am_allocator_t test_allocator = { &test_allocator_vtable, NULL };

int main(void) {
    printf("Running map copy test...\n");

    am_map_t *map = am_map_create(&test_allocator, 8);
    assert(map != NULL);

    // 插入若干键值对，包括扩容场景
    const int N = 50;
    for (int i = 0; i < N; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t v = am_make_value_of_int(-i);
        map = am_map_set(&test_allocator, map, k, v);
        assert(map != NULL);
    }

    am_map_t *copy = am_map_copy(&test_allocator, map);
    assert(copy != NULL);
    assert(copy != map);
    assert(am_map_length(&test_allocator, copy) == am_map_length(&test_allocator, map));
    assert(am_map_capacity(&test_allocator, copy) == am_map_capacity(&test_allocator, map));

    for (int i = 0; i < N; i++) {
        am_value_t k = am_make_value_of_uint((uint32_t)i);
        am_value_t expected = am_make_value_of_int(-i);
        assert(am_map_contains(&test_allocator, copy, k) == 1);
        assert(am_map_get(&test_allocator, copy, k) == expected);
    }

    // 修改副本不应影响原 map
    am_value_t k0 = am_make_value_of_uint(0);
    map = am_map_set(&test_allocator, map, k0, am_make_value_of_int(9999));
    assert(map != NULL);
    assert(am_map_get(&test_allocator, copy, k0) == am_make_value_of_int(0));

    am_map_destroy(&test_allocator, map);
    am_map_destroy(&test_allocator, copy);

    printf("Map copy test passed.\n");
    return 0;
}
