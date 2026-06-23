#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>

#include "parser.h"
#include "compiler.h"
#include "debug.h"


// ===============================================================================
// 基础设施：基于内存池的简单测试分配器（bump allocator）
// ===============================================================================

#define TEST_POOL_SIZE (8 * 1024 * 1024)

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

static const am_allocator_vtable_t test_allocator_vtable = {
    test_malloc,
    test_calloc,
    test_realloc,
    test_free,
    test_destroy
};

static am_allocator_t test_allocator = { &test_allocator_vtable, &test_allocator_state };


// ===============================================================================
// 指令打印辅助
// ===============================================================================

static const char *opcode_name(uint32_t opcode) {
    switch (opcode) {
        case AM_VM_OP_nop:         return "nop";
        case AM_VM_OP_store:       return "store";
        case AM_VM_OP_load:        return "load";
        case AM_VM_OP_loadclosure: return "loadclosure";
        case AM_VM_OP_push:        return "push";
        case AM_VM_OP_pop:         return "pop";
        case AM_VM_OP_swap:        return "swap";
        case AM_VM_OP_set:         return "set";
        case AM_VM_OP_call:        return "call";
        case AM_VM_OP_callnative:  return "callnative";
        case AM_VM_OP_tailcall:    return "tailcall";
        case AM_VM_OP_return:      return "return";
        case AM_VM_OP_capturecc:   return "capturecc";
        case AM_VM_OP_iftrue:      return "iftrue";
        case AM_VM_OP_iffalse:     return "iffalse";
        case AM_VM_OP_goto:        return "goto";
        case AM_VM_OP_read:        return "read";
        case AM_VM_OP_write:       return "write";
        case AM_VM_OP_pause:       return "pause";
        case AM_VM_OP_halt:        return "halt";
        case AM_VM_OP_fork:        return "fork";
        case AM_VM_OP_display:     return "display";
        case AM_VM_OP_newline:     return "newline";
        case AM_VM_OP_add:         return "add";
        case AM_VM_OP_sub:         return "sub";
        case AM_VM_OP_mul:         return "mul";
        case AM_VM_OP_div:         return "div";
        case AM_VM_OP_mod:         return "mod";
        case AM_VM_OP_eq:          return "eq";
        case AM_VM_OP_eqv:         return "eqv";
        case AM_VM_OP_ge:          return "ge";
        case AM_VM_OP_le:          return "le";
        case AM_VM_OP_gt:          return "gt";
        case AM_VM_OP_lt:          return "lt";
        case AM_VM_OP_not:         return "not";
        case AM_VM_OP_and:         return "and";
        case AM_VM_OP_or:          return "or";
        case AM_VM_OP_typeof:      return "typeof";
        case AM_VM_OP_car:         return "car";
        case AM_VM_OP_cdr:         return "cdr";
        case AM_VM_OP_cons:        return "cons";
        case AM_VM_OP_get_item:    return "get_item";
        case AM_VM_OP_set_item:    return "set_item";
        case AM_VM_OP_list_push:   return "list_push";
        case AM_VM_OP_list_pop:    return "list_pop";
        case AM_VM_OP_length:      return "length";
        case AM_VM_OP_concat:      return "concat";
        case AM_VM_OP_duplicate:   return "duplicate";
        default:                   return "?";
    }
}


static void print_operand(am_ast_t *ast, am_value_t op) {
    if (am_value_is_varid(op)) {
        am_varid_t v = am_value_to_varid(op);
        wchar_t *name = am_vocab_get(ast->alloc, ast->var_vocab, &v);
        printf("%ls", name ? name : L"?");
    }
    else if (am_value_is_handle(op)) {
        printf("handle_%zu", am_value_to_handle(op));
    }
    else if (am_value_is_iaddr(op)) {
        printf("iaddr_%zu", am_value_to_iaddr(op));
    }
    else if (am_value_is_label(op)) {
        printf("label_%zu", am_value_to_label(op));
    }
    else if (am_value_is_symbol(op)) {
        am_symbol_t s = am_value_to_symbol(op);
        wchar_t *name = am_vocab_get(ast->alloc, ast->symbol_vocab, &s);
        printf("%ls", name ? name : L"?");
    }
    else if (am_value_is_uint(op)) {
        printf("%llu", (unsigned long long)am_value_to_uint(op));
    }
    else if (am_value_is_int(op)) {
        printf("%lld", (long long)am_value_to_int(op));
    }
    else if (am_value_is_float(op)) {
        printf("%g", (double)am_value_to_float(op));
    }
    else if (am_value_is_boolean(op)) {
        printf("%s", am_value_to_boolean(op) ? "#t" : "#f");
    }
    else if (am_value_is_null(op)) {
        printf("#null");
    }
    else if (am_value_is_undefined(op)) {
        printf("#undefined");
    }
    else {
        printf("?");
    }
}


static void print_ilcode(am_ast_t *ast, am_instruction_t *ilcode, am_iaddr_t icount) {
    for (am_iaddr_t i = 0; i < icount; i++) {
        printf("[%4zu] %-12s ", (size_t)i, opcode_name(ilcode[i].opcode));
        print_operand(ast, ilcode[i].oprand);
        printf("\n");
    }
}


// ===============================================================================
// 主函数
// ===============================================================================

int main(void) {
    if (!setlocale(LC_ALL, "")) {
        fprintf(stderr, "setlocale failed\n");
        return 1;
    }

    test_allocator_init();

    const wchar_t *code = L"((lambda ()\n"
                          L"  (define sum 0)\n"
                          L"  (define i 1)\n"
                          L"  (while (<= i 10)\n"
                          L"    (set! sum (+ sum i))\n"
                          L"    (set! i (+ i 1)))\n"
                          L"  (define fact (lambda (n) (if (<= n 1) 1 (* n (fact (- n 1))))))\n"
                          L"  (display sum)\n"
                          L"  (display (fact 5))\n"
                          L"  (display (and #t #f #t))\n"
                          L"  (display (or #f #t #f))\n"
                          L"  (display (cond ((> 3 2) 7) (else 8)))\n"
                          L"  (display 'symbol)\n"
                          L"  (display `(1 ,(+ 1 1) 3))))";

    am_ast_t *ast = am_parser(&test_allocator, (wchar_t *)code, L"/tmp/test.scm");
    if (!ast) {
        fprintf(stderr, "Parser failed\n");
        return 1;
    }

    printf("=== AST ===\n");
    am_debug_ast_print_to_stdout(ast);
    printf("\n=== IL Code (before label resolution) ===\n");

    am_compiler_ctx_t *ctx = am_compiler_ctx_create(ast);
    if (!ctx) {
        fprintf(stderr, "Failed to create compiler context\n");
        am_ast_destroy(ast);
        return 1;
    }

    if (am_compile_all(ctx) != 0) {
        fprintf(stderr, "Compile failed\n");
        am_compiler_ctx_destroy(ctx);
        am_ast_destroy(ast);
        return 1;
    }

    print_ilcode(ast, ctx->ilcode, ctx->icount);

    printf("\n=== IL Code (after label resolution) ===\n");
    if (am_compiler_label_resolution(ctx) != 0) {
        fprintf(stderr, "Label resolution failed\n");
        am_compiler_ctx_destroy(ctx);
        am_ast_destroy(ast);
        return 1;
    }

    print_ilcode(ast, ctx->ilcode, ctx->icount);

    am_compiler_ctx_destroy(ctx);
    am_ast_destroy(ast);

    // 释放测试分配器底层内存
    free(test_allocator_state.base);
    test_allocator_state.base = NULL;

    return 0;
}
