#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wchar.h>
#include <locale.h>
#include <stdint.h>

#include "object.h"
#include "wstring.h"


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

static int wstring_equal(am_wstring_t *a, am_wstring_t *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->length != b->length) return 0;
    if (a->base.type != b->base.type) return 0;
    for (size_t i = 0; i < a->length; i++) {
        if (a->content[i] != b->content[i]) return 0;
    }
    return 1;
}

// 打印字符串对象的各个字段，用于可视化调试
static void print_wstring_fields(const char *label, am_wstring_t *ws) {
    if (!ws) {
        printf("  [%s] <NULL>\n", label);
        return;
    }
    printf("  [%s] am_wstring_t @ %p\n", label, (void *)ws);
    printf("    base.header = %u\n", ws->base.header);
    printf("    base.hash   = %u\n", ws->base.hash);
    printf("    base.gcmark = %u\n", ws->base.gcmark);
    printf("    base.type   = %d (AM_OBJECT_TYPE_WSTRING = %d)\n",
           ws->base.type, AM_OBJECT_TYPE_WSTRING);
    printf("    length      = %zu\n", ws->length);
    printf("    content     = [");
    for (size_t i = 0; i < ws->length; i++) {
        am_wchar_t wc = am_value_to_wchar(ws->content[i]);
        if (i > 0) printf(", ");
        printf("'%lc' (0x%08X / value=0x%016zX)", (wint_t)wc, (unsigned int)wc, (size_t)ws->content[i]);
    }
    printf("]\n");
}

// 将字节序列以 0xXX 十六进制格式打印，用于可视化调试
static void print_hex_buffer(const char *label, uint8_t *buffer, size_t size) {
    printf("  [%s] size=%zu bytes\n    ", label, size);
    for (size_t i = 0; i < size; i++) {
        printf("0x%02X ", buffer[i]);
        if ((i + 1) % 16 == 0 && i + 1 < size) {
            printf("\n    ");
        }
    }
    printf("\n");
}


// ===============================================================================
// 测试用例
// ===============================================================================

static void test_create_destroy(void) {
    printf("test_create_destroy ... \n");

    wchar_t text[] = L"人类的本质是复读机";
    am_wstring_t *ws = am_wstring_create(&test_allocator, text, 9);
    assert(ws != NULL);
    assert(ws->base.type == AM_OBJECT_TYPE_WSTRING);
    assert(ws->length == 9);
    assert(am_value_to_wchar(ws->content[0]) == L'人');
    assert(am_value_to_wchar(ws->content[4]) == L'质');

    print_wstring_fields("create_destroy", ws);

    assert(am_wstring_destroy(&test_allocator, ws) == 0);
    assert(am_wstring_destroy(&test_allocator, NULL) == 0);

    test_allocator_reset();
    printf("  OK\n");
}

static void test_create_empty(void) {
    printf("test_create_empty ... ");

    am_wstring_t *ws = am_wstring_create(&test_allocator, L"", 0);
    assert(ws != NULL);
    assert(ws->length == 0);
    assert(ws->base.type == AM_OBJECT_TYPE_WSTRING);
    assert(am_wstring_destroy(&test_allocator, ws) == 0);

    assert(am_wstring_create(&test_allocator, NULL, 0) == NULL);

    test_allocator_reset();
    printf("OK\n");
}

static void test_copy(void) {
    printf("test_copy ... \n");

    wchar_t text[] = L"人类的本质是复读机";
    am_wstring_t *ws = am_wstring_create(&test_allocator, text, 9);
    assert(ws != NULL);

    am_wstring_t *copy = am_wstring_copy(&test_allocator, ws);
    assert(copy != NULL);
    assert(copy != ws);
    assert(wstring_equal(ws, copy));

    print_wstring_fields("copy_original", ws);
    print_wstring_fields("copy_copy", copy);

    assert(am_wstring_copy(&test_allocator, NULL) == NULL);

    test_allocator_reset();
    printf("  OK\n");
}

static void test_dump_size_only(void) {
    printf("test_dump_size_only ... ");

    wchar_t text[] = L"人类的本质是复读机";
    am_wstring_t *ws = am_wstring_create(&test_allocator, text, 9);
    assert(ws != NULL);

    size_t size = am_wstring_dump(&test_allocator, ws, NULL, 0);
    assert(size != SIZE_MAX);
    assert(size == sizeof(size_t) + 9 * sizeof(am_value_t));

    size_t size2 = am_wstring_dump(&test_allocator, ws, NULL, SIZE_MAX);
    assert(size2 == size);

    test_allocator_reset();
    printf("OK\n");
}

static void test_dump_load_roundtrip(void) {
    printf("test_dump_load_roundtrip ... \n");

    wchar_t text[] = L"人类的本质是复读机";
    am_wstring_t *ws = am_wstring_create(&test_allocator, text, 9);
    assert(ws != NULL);

    print_wstring_fields("dump_load_roundtrip_original", ws);

    size_t size = am_wstring_dump(&test_allocator, ws, NULL, 0);
    assert(size != SIZE_MAX);

    uint8_t *buffer = (uint8_t *)malloc(size);
    assert(buffer != NULL);

    size_t written = am_wstring_dump(&test_allocator, ws, buffer, 0);
    assert(written == size);

    print_hex_buffer("dump_load_roundtrip_buffer", buffer, size);

    am_wstring_t *loaded = am_wstring_load(&test_allocator, buffer, 0);
    assert(loaded != NULL);
    assert(wstring_equal(ws, loaded));
    assert(loaded->base.type == AM_OBJECT_TYPE_WSTRING);

    print_wstring_fields("dump_load_roundtrip_loaded", loaded);

    free(buffer);
    test_allocator_reset();
    printf("  OK\n");
}

static void test_dump_load_with_offset(void) {
    printf("test_dump_load_with_offset ... \n");

    wchar_t text[] = L"人类的本质是复读机";
    am_wstring_t *ws = am_wstring_create(&test_allocator, text, 9);
    assert(ws != NULL);

    print_wstring_fields("dump_load_with_offset_original", ws);

    size_t size = am_wstring_dump(&test_allocator, ws, NULL, 0);
    assert(size != SIZE_MAX);

    uint8_t *buffer = (uint8_t *)malloc(size + 10);
    assert(buffer != NULL);
    memset(buffer, 0xAA, size + 10);

    size_t written = am_wstring_dump(&test_allocator, ws, buffer, 7);
    assert(written == size);

    print_hex_buffer("dump_load_with_offset_full_buffer", buffer, size + 10);

    am_wstring_t *loaded = am_wstring_load(&test_allocator, buffer, 7);
    assert(loaded != NULL);
    assert(wstring_equal(ws, loaded));

    print_wstring_fields("dump_load_with_offset_loaded", loaded);

    free(buffer);
    test_allocator_reset();
    printf("  OK\n");
}

static void test_load_null_buffer(void) {
    printf("test_load_null_buffer ... ");

    assert(am_wstring_load(&test_allocator, NULL, 0) == NULL);

    test_allocator_reset();
    printf("OK\n");
}


// ===============================================================================
// 主函数
// ===============================================================================

int main(void) {
    setlocale(LC_ALL, "");

    test_allocator_state.base = (uint8_t *)malloc(TEST_POOL_SIZE);
    assert(test_allocator_state.base != NULL);
    test_allocator_state.offset = 0;
    test_allocator_state.capacity = TEST_POOL_SIZE;

    printf("Running WString tests...\n");

    test_create_destroy();
    test_create_empty();
    test_copy();
    test_dump_size_only();
    test_dump_load_roundtrip();
    test_dump_load_with_offset();
    test_load_null_buffer();

    printf("All WString tests passed.\n");

    free(test_allocator_state.base);
    return 0;
}
