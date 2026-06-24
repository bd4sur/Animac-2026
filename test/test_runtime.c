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


// ===============================================================================
// 基础设施：基于内存池的简单测试分配器（bump allocator）
// ===============================================================================

#define TEST_POOL_SIZE (128 * 1024 * 1024)

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

static am_runtime_t *compile_and_run(const wchar_t *code, int expect_error) {
    test_allocator_reset(&test_vm_allocator_state);
    test_allocator_reset(&test_heap_allocator_state);
    test_halt_called = 0;
    test_error_called = 0;

    am_ast_t *ast = am_parser(&test_vm_allocator, (wchar_t *)code, L"/tmp/test.scm");
    assert(ast != NULL);

    am_parser_tail_call_analysis(ast);

    am_ast_t *linked = am_link(ast, L"/tmp");
    assert(linked != NULL);
    assert(linked == ast);

    int32_t resolution_result = am_linker_import_ref_resolution(NULL, linked);
    assert(resolution_result == 0);

    am_compiler_ctx_t *ctx = am_compiler_ctx_create(linked);
    assert(ctx != NULL);

    int32_t compile_result = am_compile_all(ctx);
    assert(compile_result == 0);

    int32_t label_result = am_compiler_label_resolution(ctx);
    assert(label_result == 0);

    am_module_t mod;
    memset(&mod, 0, sizeof(mod));
    mod.base.type = AM_OBJECT_TYPE_BASE;
    mod.opstack_depth = 1024;
    mod.ast = linked;
    mod.ilcode = ctx->ilcode;
    mod.ilcode_length = ctx->icount;

    am_runtime_t *rt = am_runtime_create(&test_vm_allocator, &test_heap_allocator, L"/tmp");
    assert(rt != NULL);
    rt->callback_on_halt = on_halt;
    rt->callback_on_error = on_error;

    am_pid_t pid = am_load_module(rt, &mod);
    assert(pid == 0);

    am_start(rt);

    if (expect_error) {
        assert(test_error_called == 1);
    }
    else {
        assert(test_halt_called == 1);
        assert(test_error_called == 0);
    }

    // 释放编译上下文和 AST，调用者负责释放 runtime
    am_compiler_ctx_destroy(ctx);
    am_ast_destroy(linked);

    return rt;
}


// ===============================================================================
// 文件读取辅助函数
// ===============================================================================

static wchar_t *read_file_as_wstring(const wchar_t *path) {
    size_t path_len = wcstombs(NULL, path, 0);
    if (path_len == (size_t)-1) return NULL;

    char *mb_path = (char *)malloc(path_len + 1);
    if (!mb_path) return NULL;
    wcstombs(mb_path, path, path_len + 1);

    FILE *f = fopen(mb_path, "rb");
    free(mb_path);
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';

    size_t wlen = mbstowcs(NULL, buf, 0);
    if (wlen == (size_t)-1) { free(buf); return NULL; }

    wchar_t *wbuf = (wchar_t *)malloc(((size_t)wlen + 1) * sizeof(wchar_t));
    if (!wbuf) { free(buf); return NULL; }
    mbstowcs(wbuf, buf, wlen + 1);
    free(buf);
    return wbuf;
}


// ===============================================================================
// 运行时测试
// ===============================================================================

static void test_runtime_display_number(void) {
    printf("test_runtime_display_number ... \n");

    const wchar_t *code = L"((lambda ()\n"
                          L"  (display 42)\n"
                          L"))\n";

    printf("=== VM output ===\n");
    am_runtime_t *rt = compile_and_run(code, 0);
    printf("\n=== VM halted ===\n");

    assert(rt->output_fifo->length == 1);
    am_runtime_destroy(rt);
    printf("OK\n");
}


static void test_runtime_factorial(void) {
    printf("test_runtime_factorial ... \n");

    const wchar_t *code = L"((lambda ()\n"
                          L"  (define fact\n"
                          L"    (lambda (n)\n"
                          L"      (if (<= n 1)\n"
                          L"          1\n"
                          L"          (* n (fact (- n 1))))))\n"
                          L"  (display (fact 5))\n"
                          L"))\n";

    printf("=== VM output ===\n");
    am_runtime_t *rt = compile_and_run(code, 0);
    printf("\n=== VM halted ===\n");

    assert(rt->output_fifo->length == 1);
    am_runtime_destroy(rt);
    printf("OK\n");
}


static void test_runtime_complex_recursion(void) {
    printf("test_runtime_complex_recursion ... \n");

    // 此用例(Man-or-Boy test)来自 test_process.c，用于验证复杂闭包和尾调用
    const wchar_t *code = L"((lambda ()\n"
                          L"(define A\n"
                          L"  (lambda (k x1 x2 x3 x4 x5)\n"
                          L"      (define B\n"
                          L"        (lambda ()\n"
                          L"            (set! k (- k 1))\n"
                          L"            (A k B x1 x2 x3 x4)))\n"
                          L"      (if (<= k 0)\n"
                          L"          (+ (x4) (x5))\n"
                          L"          (B))))\n"
                          L"(define thunk_1  (lambda () 1))\n"
                          L"(define thunk_m1 (lambda () -1))\n"
                          L"(define thunk_0  (lambda () 0))\n"
                          L"(display (A 9 thunk_1 thunk_m1 thunk_m1 thunk_1 thunk_0))\n"
                          L"))\n";

    printf("=== VM output ===\n");
    am_runtime_t *rt = compile_and_run(code, 0);
    printf("\n=== VM halted ===\n");


    am_runtime_destroy(rt);
    printf("OK\n");
}


static void test_runtime_load_from_file(void) {
    printf("test_runtime_load_from_file ... \n");
    test_allocator_reset(&test_vm_allocator_state);
    test_allocator_reset(&test_heap_allocator_state);
    test_halt_called = 0;
    test_error_called = 0;

    const wchar_t *path = L"/home/bd4sur/animac/mob.scm";
    const wchar_t *base_dir = L"/home/bd4sur/animac";

    wchar_t *file_content = read_file_as_wstring(path);
    assert(file_content != NULL);

    const wchar_t *prefix = L"((lambda () ";
    const wchar_t *suffix = L" (run) ))";
    size_t prefix_len = wcslen(prefix);
    size_t suffix_len = wcslen(suffix);
    size_t content_len = wcslen(file_content);
    size_t code_len = prefix_len + content_len + suffix_len;

    wchar_t *code = (wchar_t *)am_malloc(&test_vm_allocator, (code_len + 1) * sizeof(wchar_t));
    assert(code != NULL);
    size_t pos = 0;
    for (size_t i = 0; i < prefix_len; i++) code[pos++] = prefix[i];
    for (size_t i = 0; i < content_len; i++) code[pos++] = file_content[i];
    for (size_t i = 0; i < suffix_len; i++) code[pos++] = suffix[i];
    code[pos] = L'\0';
    free(file_content);

    am_ast_t *ast = am_parser(&test_vm_allocator, code, (wchar_t *)path);
    assert(ast != NULL);

    am_parser_tail_call_analysis(ast);

    am_ast_t *linked = am_link(ast, (wchar_t *)base_dir);
    assert(linked != NULL);
    assert(linked == ast);

    int32_t resolution_result = am_linker_import_ref_resolution(NULL, linked);
    assert(resolution_result == 0);

    am_compiler_ctx_t *ctx = am_compiler_ctx_create(linked);
    assert(ctx != NULL);

    int32_t compile_result = am_compile_all(ctx);
    assert(compile_result == 0);

    int32_t label_result = am_compiler_label_resolution(ctx);
    assert(label_result == 0);

    am_module_t mod;
    memset(&mod, 0, sizeof(mod));
    mod.base.type = AM_OBJECT_TYPE_BASE;
    mod.opstack_depth = 32;
    mod.ast = linked;
    mod.ilcode = ctx->ilcode;
    mod.ilcode_length = ctx->icount;

    am_runtime_t *rt = am_runtime_create(&test_vm_allocator, &test_heap_allocator, (wchar_t *)base_dir);
    assert(rt != NULL);
    rt->callback_on_halt = on_halt;
    rt->callback_on_error = on_error;

    am_pid_t pid = am_load_module(rt, &mod);
    assert(pid == 0);

    printf("=== VM output ===\n");
    am_start(rt);
    printf("\n=== VM halted ===\n");

    am_runtime_destroy(rt);
    am_compiler_ctx_destroy(ctx);
    am_ast_destroy(linked);
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

    // test_runtime_display_number();
    // test_runtime_factorial();
    // test_runtime_complex_recursion();
    test_runtime_load_from_file();

    test_destroy(&test_vm_allocator_state);
    test_destroy(&test_heap_allocator_state);

    printf("\nAll runtime tests passed.\n");
    return 0;
}
