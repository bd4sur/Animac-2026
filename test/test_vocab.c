#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wchar.h>
#include <stdint.h>

#include "object.h"
#include "vocab.h"


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

static int vocab_equal(am_vocab_t *a, am_vocab_t *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->length != b->length) return 0;
    if (a->base.type != b->base.type) return 0;
    for (size_t i = 0; i < a->length; i++) {
        if (wcscmp(a->words[i], b->words[i]) != 0) return 0;
    }
    return 1;
}


// ===============================================================================
// 测试用例
// ===============================================================================

static void test_dump_load_empty(void) {
    printf("test_dump_load_empty ... ");

    am_vocab_t *vocab = am_vocab_create(&test_allocator, 8);
    assert(vocab != NULL);

    size_t size = am_vocab_dump(&test_allocator, vocab, NULL, 0);
    assert(size != SIZE_MAX);
    assert(size == sizeof(am_vocab_t));

    uint8_t *buffer = (uint8_t *)malloc(size);
    assert(buffer != NULL);
    assert(am_vocab_dump(&test_allocator, vocab, buffer, 0) == size);

    am_vocab_t *loaded = am_vocab_load(&test_allocator, buffer, 0);
    assert(loaded != NULL);
    assert(vocab_equal(vocab, loaded));
    assert(loaded->base.type == AM_OBJECT_TYPE_VOCAB);
    assert(loaded->length == 0);

    free(buffer);
    test_allocator_reset();
    printf("OK\n");
}

static void test_dump_load_with_words(void) {
    printf("test_dump_load_with_words ... ");

    am_vocab_t *vocab = am_vocab_create(&test_allocator, 8);
    assert(vocab != NULL);

    wchar_t *words[] = { L"lambda", L"define", L"set!", L"import" };
    for (size_t i = 0; i < 4; i++) {
        size_t idx;
        vocab = am_vocab_insert(&test_allocator, vocab, words[i], &idx);
        assert(vocab != NULL);
        assert(idx != SIZE_MAX);
    }

    size_t size = am_vocab_dump(&test_allocator, vocab, NULL, 0);
    assert(size != SIZE_MAX);

    uint8_t *buffer = (uint8_t *)malloc(size);
    assert(buffer != NULL);
    assert(am_vocab_dump(&test_allocator, vocab, buffer, 0) == size);

    am_vocab_t *loaded = am_vocab_load(&test_allocator, buffer, 0);
    assert(loaded != NULL);
    assert(vocab_equal(vocab, loaded));
    assert(loaded->base.type == AM_OBJECT_TYPE_VOCAB);
    assert(loaded->length == 4);

    // 验证加载后的 words 指针指向加载对象内部，且能正常查找
    for (size_t i = 0; i < 4; i++) {
        assert(am_vocab_find(&test_allocator, loaded, words[i]) == i);
    }

    free(buffer);
    test_allocator_reset();
    printf("OK\n");
}

static void test_dump_load_with_offset(void) {
    printf("test_dump_load_with_offset ... ");

    am_vocab_t *vocab = am_vocab_create(&test_allocator, 4);
    assert(vocab != NULL);
    size_t tmp_idx;
    vocab = am_vocab_insert(&test_allocator, vocab, L"hello", &tmp_idx);
    assert(vocab != NULL);
    vocab = am_vocab_insert(&test_allocator, vocab, L"world", &tmp_idx);
    assert(vocab != NULL);

    size_t size = am_vocab_dump(&test_allocator, vocab, NULL, 0);
    assert(size != SIZE_MAX);

    uint8_t *buffer = (uint8_t *)malloc(size + 12);
    assert(buffer != NULL);
    memset(buffer, 0xCC, size + 12);

    assert(am_vocab_dump(&test_allocator, vocab, buffer, 12) == size);

    am_vocab_t *loaded = am_vocab_load(&test_allocator, buffer, 12);
    assert(loaded != NULL);
    assert(vocab_equal(vocab, loaded));

    free(buffer);
    test_allocator_reset();
    printf("OK\n");
}

static void test_load_invalid_type(void) {
    printf("test_load_invalid_type ... ");

    uint8_t buffer[64];
    memset(buffer, 0, sizeof(buffer));
    am_vocab_t *fake = (am_vocab_t *)buffer;
    fake->base.type = AM_OBJECT_TYPE_LIST;
    fake->length = 0;

    assert(am_vocab_load(&test_allocator, buffer, 0) == NULL);

    test_allocator_reset();
    printf("OK\n");
}

static void test_load_null_buffer(void) {
    printf("test_load_null_buffer ... ");

    assert(am_vocab_load(&test_allocator, NULL, 0) == NULL);

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

    printf("Running vocab dump/load tests...\n");

    test_dump_load_empty();
    test_dump_load_with_words();
    test_dump_load_with_offset();
    test_load_invalid_type();
    test_load_null_buffer();

    printf("All vocab dump/load tests passed.\n");

    free(test_allocator_state.base);
    return 0;
}
