#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "object.h"
#include "allocator.h"
#include "vocab.h"


// ===============================================================================
// 内部辅助函数
// ===============================================================================

static am_vocab_t *am_vocab_resize(am_allocator_t *alloc, am_vocab_t *vocab, size_t new_capacity) {
    if (new_capacity < vocab->length) new_capacity = vocab->length;

    size_t total_size = sizeof(am_vocab_t) + new_capacity * sizeof(wchar_t *);
    am_vocab_t *new_vocab = (am_vocab_t *)am_malloc(alloc, total_size);
    if (!new_vocab) return NULL;

    new_vocab->base = vocab->base;
    new_vocab->capacity = new_capacity;
    new_vocab->length = vocab->length;

    if (vocab->length > 0) {
        memcpy(new_vocab->words, vocab->words, vocab->length * sizeof(wchar_t *));
    }

    am_free(alloc, vocab);
    return new_vocab;
}


static am_vocab_t *am_vocab_grow_if_needed(am_allocator_t *alloc, am_vocab_t *vocab) {
    if (vocab->length < vocab->capacity) return vocab;

    size_t new_capacity = vocab->capacity * 2;
    if (new_capacity < 8) new_capacity = 8;
    return am_vocab_resize(alloc, vocab, new_capacity);
}


// ===============================================================================
// 构造函数
// ===============================================================================

am_vocab_t *am_vocab_create(am_allocator_t *alloc, size_t capacity) {
    if (capacity < 4) capacity = 4;

    size_t total_size = sizeof(am_vocab_t) + capacity * sizeof(wchar_t *);
    am_vocab_t *vocab = (am_vocab_t *)am_calloc(alloc, total_size);
    if (!vocab) return NULL;

    vocab->base.type = AM_OBJECT_TYPE_BASE;
    vocab->capacity = capacity;
    vocab->length = 0;

    return vocab;
}


// ===============================================================================
// 析构
// ===============================================================================

int32_t am_vocab_destroy(am_allocator_t *alloc, am_vocab_t *vocab) {
    if (!vocab) return 0;
    for (size_t i = 0; i < vocab->length; i++) {
        if (vocab->words[i]) am_free(alloc, vocab->words[i]);
    }
    am_free(alloc, vocab);
    return 1;
}


// ===============================================================================
// 拷贝
// ===============================================================================

am_vocab_t *am_vocab_copy(am_allocator_t *alloc, am_vocab_t *vocab) {
    if (!vocab) return NULL;

    am_vocab_t *copy = am_vocab_create(alloc, vocab->capacity);
    if (!copy) return NULL;

    copy->base = vocab->base;
    copy->length = vocab->length;

    for (size_t i = 0; i < vocab->length; i++) {
        size_t len = wcslen(vocab->words[i]);
        copy->words[i] = (wchar_t *)am_malloc(alloc, (len + 1) * sizeof(wchar_t));
        if (!copy->words[i]) {
            am_vocab_destroy(alloc, copy);
            return NULL;
        }
        wcscpy(copy->words[i], vocab->words[i]);
    }

    return copy;
}


// ===============================================================================
// 对象二进制转储
// ===============================================================================

uint8_t *am_vocab_dump(am_allocator_t *alloc, am_vocab_t *vocab, size_t *size) {
    (void)alloc;
    if (!vocab) {
        if (size) *size = 0;
        return NULL;
    }

    size_t content_size = 0;
    for (size_t i = 0; i < vocab->length; i++) {
        content_size += (wcslen(vocab->words[i]) + 1) * sizeof(wchar_t);
    }

    size_t dump_size = sizeof(am_vocab_t) + vocab->length * sizeof(wchar_t *) + content_size;
    uint8_t *buf = (uint8_t *)malloc(dump_size);
    if (!buf) {
        if (size) *size = 0;
        return NULL;
    }

    am_vocab_t *dump = (am_vocab_t *)buf;
    dump->base = vocab->base;
    dump->capacity = vocab->length;
    dump->length = vocab->length;

    wchar_t *content_ptr = (wchar_t *)(buf + sizeof(am_vocab_t) + vocab->length * sizeof(wchar_t *));
    for (size_t i = 0; i < vocab->length; i++) {
        size_t len = wcslen(vocab->words[i]);
        dump->words[i] = content_ptr; // 运行时指针，指向 dump 内部的字符串区域
        wcscpy(content_ptr, vocab->words[i]);
        content_ptr += (len + 1);
    }

    if (size) *size = dump_size;
    return buf;
}


// ===============================================================================
// 基本操作
// ===============================================================================

size_t am_vocab_find(am_allocator_t *alloc, am_vocab_t *vocab, wchar_t *word) {
    (void)alloc;
    if (!vocab || !word) return SIZE_MAX;
    for (size_t i = 0; i < vocab->length; i++) {
        if (vocab->words[i] && wcscmp(vocab->words[i], word) == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}


size_t am_vocab_insert(am_allocator_t *alloc, am_vocab_t *vocab, wchar_t *word) {
    if (!vocab || !word) return SIZE_MAX;

    size_t existing = am_vocab_find(alloc, vocab, word);
    if (existing != SIZE_MAX) return existing;

    vocab = am_vocab_grow_if_needed(alloc, vocab);
    if (!vocab) return SIZE_MAX;

    size_t len = wcslen(word);
    vocab->words[vocab->length] = (wchar_t *)am_malloc(alloc, (len + 1) * sizeof(wchar_t));
    if (!vocab->words[vocab->length]) return SIZE_MAX;

    wcscpy(vocab->words[vocab->length], word);
    return vocab->length++;
}


wchar_t *am_vocab_get(am_allocator_t *alloc, am_vocab_t *vocab, size_t *index) {
    (void)alloc;
    if (!vocab || !index || *index >= vocab->length) return NULL;
    return vocab->words[*index];
}
