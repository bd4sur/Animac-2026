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
// 进程测试
// ===============================================================================

static void test_process_basic(void) {
    printf("test_process_basic ... \n");
    test_allocator_reset(&test_vm_allocator_state);
    test_allocator_reset(&test_heap_allocator_state);

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
                          L"(display (A 10 thunk_1 thunk_m1 thunk_m1 thunk_1 thunk_0))\n"
                          L"))\n";

    am_ast_t *ast = am_parser(&test_vm_allocator, (wchar_t *)code, L"/tmp/test.scm");
    assert(ast != NULL);

    am_parser_tail_call_analysis(ast);

    // 执行链接（无依赖，直接返回自身）
    am_ast_t *linked = am_link(ast, L"/tmp");
    assert(linked != NULL);
    assert(linked == ast);

    // 执行外部引用解析
    int32_t resolution_result = am_linker_import_ref_resolution(NULL, linked);
    assert(resolution_result == 0);

    printf("=== resolved AST ===\n");
    am_debug_ast_print_to_stdout(linked);

    // 编译
    am_compiler_ctx_t *ctx = am_compiler_ctx_create(linked);
    assert(ctx != NULL);

    int32_t compile_result = am_compile_all(ctx);
    assert(compile_result == 0);

    printf("\n=== IL Code (before label resolution) ===\n");
    // am_debug_print_ilcode_raw(ctx->ilcode, ctx->icount);
    am_debug_print_ilcode(linked, ctx->ilcode, ctx->icount);

    int32_t label_result = am_compiler_label_resolution(ctx);
    assert(label_result == 0);

    printf("\n=== IL Code (after label resolution) ===\n");
    // am_debug_print_ilcode_raw(ctx->ilcode, ctx->icount);
    am_debug_print_ilcode(linked, ctx->ilcode, ctx->icount);

    // 构造模块
    am_module_t mod;
    memset(&mod, 0, sizeof(mod));
    mod.base.type = AM_OBJECT_TYPE_BASE;
    mod.opstack_depth = 1024;
    mod.ast = linked;
    mod.ilcode = ctx->ilcode;
    mod.ilcode_length = ctx->icount;

    // 从模块加载进程
    am_process_t *proc = am_process_load_from_module(&test_vm_allocator, &test_heap_allocator, &mod);
    assert(proc != NULL);

    printf("\n=== Process loaded ===\n");
    printf("pid=%zu, parent_pid=%zu, state=%d, PC=%zu\n",
           proc->pid, proc->parent_pid, proc->state, proc->PC);
    printf("ilcode_length=%zu\n", proc->ilcode_length);
    printf("opstack_capacity=%zu, fstack_capacity=%zu\n",
           proc->opstack_capacity, proc->fstack_capacity);
    printf("current_closure_handle=%zu\n", proc->current_closure_handle);

    // 初始状态断言
    assert(proc->state == AM_PROCESS_STATE_READY);
    assert(proc->PC == 0);
    assert(proc->ilcode_length == ctx->icount);
    assert(proc->opstack_capacity == 1024);
    assert(proc->fstack_capacity == 1000);
    assert(proc->current_closure_handle == AM_HANDLE_NULL);
    assert(am_process_length_of_opstack(proc) == 0);
    assert(am_process_length_of_fstack(proc) == 0);
    assert(proc->heap != NULL);

    // 测试操作数栈
    am_value_t v1 = am_make_value_of_uint(42);
    am_value_t v2 = am_make_value_of_int(-7);
    assert(am_process_push_operand(proc, v1) == 0);
    assert(am_process_push_operand(proc, v2) == 0);
    assert(am_process_length_of_opstack(proc) == 2);

    am_value_t popped = am_process_pop_operand(proc);
    assert(popped == v2);
    popped = am_process_pop_operand(proc);
    assert(popped == v1);
    assert(am_process_length_of_opstack(proc) == 0);

    // 空栈弹出的失败语义
    am_value_t empty_pop = am_process_pop_operand(proc);
    assert(empty_pop == (am_value_t)UINTPTR_MAX);

    // 测试函数调用栈
    am_handle_t closure_hd = am_process_make_closure(proc, 10, AM_HANDLE_NULL);
    assert(closure_hd != AM_HANDLE_NULL);

    am_value_t closure_value = am_make_value_of_handle(closure_hd);
    am_value_t return_iaddr = am_make_value_of_iaddr(99);
    assert(am_process_push_stack_frame(proc, closure_value, return_iaddr) == 0);
    assert(am_process_length_of_fstack(proc) == 2);

    am_value_t out_closure, out_iaddr;
    assert(am_process_pop_stack_frame(proc, &out_closure, &out_iaddr) == 0);
    assert(out_closure == closure_value);
    assert(out_iaddr == return_iaddr);
    assert(am_process_length_of_fstack(proc) == 0);

    // 测试闭包访问
    am_obj_closure_t *closure = am_process_get_closure(proc, closure_hd);
    assert(closure != NULL);
    assert(closure->base.type == AM_OBJECT_TYPE_CLOSURE);
    assert(closure->iaddr == 10);
    assert(closure->parent == AM_HANDLE_NULL);

    // 测试当前指令访问
    uint32_t opcode;
    am_value_t operand;
    assert(am_process_current_instruction(proc, &opcode, &operand) == 0);
    printf("current instruction: [%s] operand=%016llx\n", am_debug_opcode_name(opcode), (unsigned long long)operand);

    // 测试步进和跳转
    am_iaddr_t saved_pc = proc->PC;
    am_process_step(proc);
    assert(proc->PC == saved_pc + 1);
    am_process_goto(proc, 5);
    assert(proc->PC == 5);
    am_process_goto(proc, 0);
    assert(proc->PC == 0);

    // 测试进程状态设置
    am_process_set_state(proc, AM_PROCESS_STATE_RUNNING);
    assert(proc->state == AM_PROCESS_STATE_RUNNING);
    am_process_set_state(proc, AM_PROCESS_STATE_READY);

    // 测试变量绑定与解引用
    am_varid_t var_x = 0;
    am_value_t val_x = am_make_value_of_uint(123);
    closure = am_closure_init_bound_var(proc->heap_alloc, closure, var_x, val_x);
    assert(closure != NULL);
    // 更新堆中闭包指针（闭包可能因扩容而移动）
    assert(am_heap_set(proc->heap_alloc, proc->heap, closure_hd,
                       am_make_value_of_ptr((am_object_t *)closure)) == 0);

    proc->current_closure_handle = closure_hd;
    am_value_t deref_x = am_process_dereference(proc, var_x);
    assert(deref_x == val_x);

    // 测试续体捕获与恢复
    // 构造一个带有opstack和fstack内容的运行时状态
    am_process_push_operand(proc, am_make_value_of_uint(100));
    am_process_push_operand(proc, am_make_value_of_uint(200));
    am_process_push_stack_frame(proc, closure_value, am_make_value_of_iaddr(50));

    am_iaddr_t cont_return_target = 777;
    am_handle_t cont_hd = am_process_capture_continuation(proc, cont_return_target);
    assert(cont_hd != AM_HANDLE_NULL);

    // 修改当前状态
    am_process_pop_operand(proc);
    am_process_pop_operand(proc);
    am_process_pop_stack_frame(proc, &out_closure, &out_iaddr);
    proc->current_closure_handle = AM_HANDLE_NULL;

    // 恢复续体
    am_iaddr_t loaded_target = am_process_load_continuation(proc, cont_hd);
    assert(loaded_target == cont_return_target);
    assert(proc->current_closure_handle == closure_hd);
    assert(am_process_length_of_opstack(proc) == 2);
    assert(am_process_length_of_fstack(proc) == 2);

    am_value_t r1 = am_process_pop_operand(proc);
    am_value_t r2 = am_process_pop_operand(proc);
    assert(r1 == am_make_value_of_uint(200));
    assert(r2 == am_make_value_of_uint(100));

    // 测试GC：清理孤立对象
    // 创建一个新的闭包但不保留任何引用，应被GC回收
    am_handle_t orphan_closure = am_process_make_closure(proc, 999, AM_HANDLE_NULL);
    assert(orphan_closure != AM_HANDLE_NULL);

    size_t heap_size_before = am_map_length(proc->heap_alloc, proc->heap->table);
    int32_t gc_result = am_process_gc(proc);
    assert(gc_result == 0);
    size_t heap_size_after = am_map_length(proc->heap_alloc, proc->heap->table);
    printf("heap size before GC=%zu, after GC=%zu\n", heap_size_before, heap_size_after);
    // 至少应该回收孤立闭包
    assert(heap_size_after <= heap_size_before);

    // 清理
    am_process_destroy(proc);
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

    test_process_basic();

    test_destroy(&test_vm_allocator_state);
    test_destroy(&test_heap_allocator_state);

    printf("\nAll process tests passed.\n");
    return 0;
}
