#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <assert.h>

#include "parser.h"
#include "linker.h"
#include "compiler.h"
#include "module.h"
#include "process.h"
#include "runtime.h"
#include "debug.h"
#include "ast.h"
#include "wstring.h"


// ===============================================================================
// 基础设施：基于内存池的简单测试分配器（bump allocator）
// ===============================================================================

#define TEST_POOL_SIZE (32 * 1024 * 1024)

typedef struct test_allocator_state_t {
    uint8_t *base;
    size_t offset;
    size_t capacity;
} test_allocator_state_t;

static test_allocator_state_t test_vm_allocator_state;
static test_allocator_state_t test_heap_allocator_state;

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

static void test_destroy(void *state) {
    test_allocator_state_t *s = (test_allocator_state_t *)state;
    if (s->base) {
        free(s->base);
        s->base = NULL;
    }
    s->offset = 0;
    s->capacity = 0;
}

static void test_allocator_init(test_allocator_state_t *state) {
    state->base = (uint8_t *)malloc(TEST_POOL_SIZE);
    state->offset = 0;
    state->capacity = TEST_POOL_SIZE;
}

static void test_allocator_reset(test_allocator_state_t *state) {
    state->offset = 0;
}

static const am_allocator_vtable_t test_allocator_vtable = {
    test_malloc,
    test_calloc,
    test_realloc,
    test_free,
    test_destroy
};

static am_allocator_t test_vm_allocator = { &test_allocator_vtable, &test_vm_allocator_state };
static am_allocator_t test_heap_allocator = { &test_allocator_vtable, &test_heap_allocator_state };


// ===============================================================================
// 辅助函数
// ===============================================================================

static int test_halt_called = 0;
static int test_error_called = 0;

static void on_halt(am_runtime_t *rt) {
    (void)rt;
    test_halt_called = 1;
}

static void on_error(am_runtime_t *rt) {
    (void)rt;
    test_error_called = 1;
}


static am_runtime_t *compile_and_run(const wchar_t *code) {
    test_allocator_reset(&test_vm_allocator_state);
    test_allocator_reset(&test_heap_allocator_state);
    test_halt_called = 0;
    test_error_called = 0;

    am_ast_t *ast = am_parse(&test_vm_allocator, (wchar_t *)code, L"/tmp/test.scm");
    assert(ast != NULL);

    am_parser_tail_call_analysis(ast);

    am_ast_t *linked = am_link(ast, L"/tmp");
    assert(linked != NULL);
    assert(linked == ast);

    am_module_t *mod = am_compile(linked);
    assert(mod != NULL);

    am_runtime_t *rt = am_runtime_create(&test_vm_allocator, &test_heap_allocator, L"/tmp");
    assert(rt != NULL);
    rt->callback_on_halt = on_halt;
    rt->callback_on_error = on_error;

    am_pid_t pid = am_load_module(rt, mod);
    assert(pid == 0);

    am_start(rt);

    assert(test_halt_called == 1);
    assert(test_error_called == 0);

    // 释放模块和 AST，调用者负责释放 runtime
    free(mod->ilcode);
    am_free(linked->alloc, mod);
    am_ast_destroy(linked);

    return rt;
}


static const wchar_t *get_output_text(am_runtime_t *rt) {
    static wchar_t buf[4096];
    buf[0] = L'\0';
    size_t pos = 0;
    if (!rt->output_fifo) {
        buf[pos] = L'\0';
        return buf;
    }
    for (size_t i = 0; i < rt->output_fifo->length; i++) {
        am_value_t v = am_list_get(rt->vm_alloc, rt->output_fifo, i);
        if (!am_value_is_ptr(v)) continue;
        am_object_t *obj = am_value_to_ptr(v);
        if (obj->type != AM_OBJECT_TYPE_WSTRING) continue;
        am_wstring_t *ws = (am_wstring_t *)obj;
        for (size_t j = 0; j < ws->length && pos < 4095; j++) {
            buf[pos++] = (wchar_t)am_value_to_wchar(ws->content[j]);
        }
    }
    buf[pos] = L'\0';
    return buf;
}


static void assert_output_equals(am_runtime_t *rt, const wchar_t *expected) {
    const wchar_t *actual = get_output_text(rt);
    if (wcscmp(actual, expected) != 0) {
        fprintf(stderr, "ASSERT FAILED\n  expected: %ls\n  actual:   %ls\n", expected, actual);
        assert(0);
    }
}


// ===============================================================================
// 新引入指令的测试
// ===============================================================================

static void test_op_pow(void) {
    printf("test_op_pow ... \n");
    am_runtime_t *rt = compile_and_run(L"((lambda () (display (pow 2 3))))");
    assert_output_equals(rt, L"8");
    am_runtime_destroy(rt);
    printf("OK\n");
}


static void test_op_equal(void) {
    printf("test_op_equal ... \n");

    am_runtime_t *rt = compile_and_run(L"((lambda () (display (equal? 42 42))))");
    assert_output_equals(rt, L"#t");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (equal? 42 43))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (equal? 3.14 3.14))))");
    assert_output_equals(rt, L"#t");
    am_runtime_destroy(rt);

    printf("OK\n");
}


static void test_op_isnull(void) {
    printf("test_op_isnull ... \n");

    am_runtime_t *rt = compile_and_run(L"((lambda () (display (null? '()))))");
    assert_output_equals(rt, L"#t");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (null? 42))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (null? `(1)))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    printf("OK\n");
}


static void test_op_isundef(void) {
    printf("test_op_isundef ... \n");

    am_runtime_t *rt = compile_and_run(L"((lambda () (display (undefined? #undefined))))");
    assert_output_equals(rt, L"#t");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (undefined? 42))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    printf("OK\n");
}


static void test_op_isatom(void) {
    printf("test_op_isatom ... \n");

    am_runtime_t *rt = compile_and_run(L"((lambda () (display (atom? 42))))");
    assert_output_equals(rt, L"#t");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (atom? \"hello\"))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (atom? `(1)))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    printf("OK\n");
}


static void test_op_islist(void) {
    printf("test_op_islist ... \n");

    am_runtime_t *rt = compile_and_run(L"((lambda () (display (list? 42))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (list? \"hello\"))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (list? `(1 2)))))");
    assert_output_equals(rt, L"#t");
    am_runtime_destroy(rt);

    printf("OK\n");
}


static void test_op_isnumber(void) {
    printf("test_op_isnumber ... \n");

    am_runtime_t *rt = compile_and_run(L"((lambda () (display (number? 42))))");
    assert_output_equals(rt, L"#t");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (number? \"hello\"))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display (number? `(1)))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    printf("OK\n");
}


static void test_op_isnan(void) {
    printf("test_op_isnan ... \n");

    am_runtime_t *rt = compile_and_run(L"((lambda () (display (nan? 0.0))))");
    assert_output_equals(rt, L"#f");
    am_runtime_destroy(rt);

    printf("OK\n");
}


static void test_op_display(void) {
    printf("test_op_display ... \n");

    am_runtime_t *rt = compile_and_run(L"((lambda () (display 42)))");
    assert_output_equals(rt, L"42");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display \"hello\")))");
    assert_output_equals(rt, L"hello");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display #t)))");
    assert_output_equals(rt, L"#t");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display #null)))");
    assert_output_equals(rt, L"#null");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display '())))");
    assert_output_equals(rt, L"'()");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display `(1 2))))");
    assert_output_equals(rt, L"(1 2)");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display '(1 2))))");
    assert_output_equals(rt, L"(1 2)");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display '(a b))))");
    assert_output_equals(rt, L"(a b)");
    am_runtime_destroy(rt);

    rt = compile_and_run(L"((lambda () (display '(1 (2 3)))))");
    assert_output_equals(rt, L"(1 (2 3))");
    am_runtime_destroy(rt);

    // Quine：输出自身代码
    rt = compile_and_run(L"((lambda () (display ((lambda (x) (cons x (cons (cons quote (cons x '())) '()))) (quote (lambda (x) (cons x (cons (cons quote (cons x '())) '()))))))))");
    assert_output_equals(rt, L"((lambda (x) (cons x (cons (cons quote (cons x '())) '()))) (quote (lambda (x) (cons x (cons (cons quote (cons x '())) '())))))");
    am_runtime_destroy(rt);

    printf("OK\n");
}


// ===============================================================================
// 主函数
// ===============================================================================

int main(void) {
    if (!setlocale(LC_ALL, "")) {
        fprintf(stderr, "setlocale failed\n");
        return 1;
    }

    test_allocator_init(&test_vm_allocator_state);
    test_allocator_init(&test_heap_allocator_state);

    test_op_pow();
    test_op_equal();
    test_op_isnull();
    test_op_isundef();
    test_op_isatom();
    test_op_islist();
    test_op_isnumber();
    test_op_isnan();
    test_op_display();

    test_destroy(&test_vm_allocator_state);
    test_destroy(&test_heap_allocator_state);

    printf("\nAll new-opcode runtime tests passed.\n");
    return 0;
}
