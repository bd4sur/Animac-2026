#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// object.h 中部分内联函数存在未使用参数，属于已有代码的警告；
// 仅在本测试文件中临时忽略，不影响 continuation.h 的实现。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "continuation.h"
#pragma GCC diagnostic pop

// ===============================================================================
// 基础设施：实现一个委托给系统 malloc/free/realloc 的简单抽象分配器
// ===============================================================================

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

static void test_create_and_layout(void) {
    printf("test_create_and_layout ... ");

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
        &test_allocator, 42, 7,
        opstack, 3, fstack, 2);
    assert(cont != NULL);
    assert(cont->base.type == AM_OBJECT_TYPE_CONTINUATION);
    assert(cont->length == 5);
    assert(cont->fstack_offset == 3);
    assert(cont->cont_return_target == 42);
    assert(cont->current_closure_handle == 7);

    // 验证 opstack 区段
    assert(cont->stacks[0] == opstack[0]);
    assert(cont->stacks[1] == opstack[1]);
    assert(cont->stacks[2] == opstack[2]);

    // 验证 fstack 区段
    assert(cont->stacks[3] == fstack[0]);
    assert(cont->stacks[4] == fstack[1]);

    am_continuation_destroy(&test_allocator, cont);
    printf("OK\n");
}

static void test_empty_stacks(void) {
    printf("test_empty_stacks ... ");

    am_continuation_t *cont = am_continuation_create(
        &test_allocator, 1, 2,
        NULL, 0, NULL, 0);
    assert(cont != NULL);
    assert(cont->base.type == AM_OBJECT_TYPE_CONTINUATION);
    assert(cont->length == 0);
    assert(cont->fstack_offset == 0);
    assert(cont->cont_return_target == 1);
    assert(cont->current_closure_handle == 2);

    am_continuation_destroy(&test_allocator, cont);
    printf("OK\n");
}

static void test_copy(void) {
    printf("test_copy ... ");

    am_value_t opstack[] = {
        am_make_value_of_uint(1),
        am_make_value_of_uint(2)
    };
    am_value_t fstack[] = {
        am_make_value_of_uint(3)
    };

    am_continuation_t *cont = am_continuation_create(
        &test_allocator, 99, 11,
        opstack, 2, fstack, 1);
    assert(cont != NULL);

    am_continuation_t *copy = am_continuation_copy(&test_allocator, cont);
    assert(copy != NULL);
    assert(copy != cont);
    assert(copy->base.type == AM_OBJECT_TYPE_CONTINUATION);
    assert(copy->length == cont->length);
    assert(copy->fstack_offset == cont->fstack_offset);
    assert(copy->cont_return_target == cont->cont_return_target);
    assert(copy->current_closure_handle == cont->current_closure_handle);

    for (size_t i = 0; i < cont->length; i++) {
        assert(copy->stacks[i] == cont->stacks[i]);
    }

    am_continuation_destroy(&test_allocator, cont);
    am_continuation_destroy(&test_allocator, copy);
    printf("OK\n");
}

static void test_get_opstack_and_fstack(void) {
    printf("test_get_opstack_and_fstack ... ");

    am_value_t opstack[] = {
        am_make_value_of_uint(10),
        am_make_value_of_uint(20)
    };
    am_value_t fstack[] = {
        am_make_value_of_uint(100),
        am_make_value_of_uint(200),
        am_make_value_of_uint(300)
    };

    am_continuation_t *cont = am_continuation_create(
        &test_allocator, 5, 6,
        opstack, 2, fstack, 3);
    assert(cont != NULL);

    size_t op_len = 0;
    am_value_t *op_out = am_continuation_get_opstack(&test_allocator, cont, &op_len);
    assert(op_out != NULL);
    assert(op_len == 2);
    assert(op_out[0] == opstack[0]);
    assert(op_out[1] == opstack[1]);

    size_t f_len = 0;
    am_value_t *f_out = am_continuation_get_fstack(&test_allocator, cont, &f_len);
    assert(f_out != NULL);
    assert(f_len == 3);
    assert(f_out[0] == fstack[0]);
    assert(f_out[1] == fstack[1]);
    assert(f_out[2] == fstack[2]);

    free(op_out);
    free(f_out);
    am_continuation_destroy(&test_allocator, cont);
    printf("OK\n");
}

static void test_get_empty_stacks(void) {
    printf("test_get_empty_stacks ... ");

    am_continuation_t *cont = am_continuation_create(
        &test_allocator, 0, 0,
        NULL, 0, NULL, 0);
    assert(cont != NULL);

    size_t op_len = 123;
    am_value_t *op_out = am_continuation_get_opstack(&test_allocator, cont, &op_len);
    assert(op_out != NULL);
    assert(op_len == 0);

    size_t f_len = 456;
    am_value_t *f_out = am_continuation_get_fstack(&test_allocator, cont, &f_len);
    assert(f_out != NULL);
    assert(f_len == 0);

    free(op_out);
    free(f_out);
    am_continuation_destroy(&test_allocator, cont);
    printf("OK\n");
}

static void test_invalid_inputs(void) {
    printf("test_invalid_inputs ... ");

    assert(am_continuation_create(NULL, 0, 0, NULL, 0, NULL, 0) == NULL);

    am_continuation_t *cont = am_continuation_create(
        &test_allocator, 0, 0, NULL, 0, NULL, 0);
    assert(cont != NULL);

    size_t len = 0;
    assert(am_continuation_get_opstack(NULL, cont, &len) == NULL);
    assert(am_continuation_get_opstack(&test_allocator, NULL, &len) == NULL);
    assert(am_continuation_get_opstack(&test_allocator, cont, NULL) == NULL);

    assert(am_continuation_get_fstack(NULL, cont, &len) == NULL);
    assert(am_continuation_get_fstack(&test_allocator, NULL, &len) == NULL);
    assert(am_continuation_get_fstack(&test_allocator, cont, NULL) == NULL);

    assert(am_continuation_copy(NULL, cont) == NULL);
    assert(am_continuation_copy(&test_allocator, NULL) == NULL);

    assert(am_continuation_destroy(NULL, cont) == -1);
    assert(am_continuation_destroy(&test_allocator, NULL) == -1);

    am_continuation_destroy(&test_allocator, cont);
    printf("OK\n");
}

// ===============================================================================
// 主函数
// ===============================================================================

int main(void) {
    test_create_and_layout();
    test_empty_stacks();
    test_copy();
    test_get_opstack_and_fstack();
    test_get_empty_stacks();
    test_invalid_inputs();

    printf("\nAll continuation tests passed.\n");
    return 0;
}
