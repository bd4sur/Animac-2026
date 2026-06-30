#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <assert.h>

#include "utils.h"
#include "parser.h"
#include "linker.h"
#include "compiler.h"
#include "module.h"
#include "process.h"
#include "runtime.h"
#include "debug.h"
#include "ast.h"
#include "highlight.h"

#include "native_System.h"
#include "native_Math.h"
#include "native_String.h"

// 打印 token 类型名称
const wchar_t* type_name(int32_t type) {
    switch(type) {
        case AM_TOKEN_TYPE_DELIMITER: return L"SEP";
        case AM_TOKEN_TYPE_LB: return L"LB";
        case AM_TOKEN_TYPE_RB: return L"RB";
        case AM_TOKEN_TYPE_KEYWORD: return L"KEYWORD";
        case AM_TOKEN_TYPE_BOOLEAN: return L"BOOLEAN";
        case AM_TOKEN_TYPE_UNDEFINED: return L"UNDEFINED";
        case AM_TOKEN_TYPE_NULL: return L"NULL";
        case AM_TOKEN_TYPE_NUMBER: return L"NUMBER";
        case AM_TOKEN_TYPE_SYMBOL: return L"SYMBOL";
        case AM_TOKEN_TYPE_IDENTIFIER: return L"IDENTIFIER";
        case AM_TOKEN_TYPE_STRING: return L"STRING";
        case AM_TOKEN_TYPE_QUOTE: return L"QUOTE";
        case AM_TOKEN_TYPE_QUASIQUOTE: return L"QUASIQUOTE";
        case AM_TOKEN_TYPE_UNQUOTE: return L"UNQUOTE";
        default: return L"UNEXPECTED";
    }
}

// ===============================================================================
// 基础设施：基于统一内存池的双分配器
// ===============================================================================

static am_allocator_pool_t *g_pool = NULL;
static am_allocator_t *vm_alloc = NULL;
static am_allocator_t *heap_alloc = NULL;


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


static void test_runtime_load_from_file(char *path) {
    printf("test_runtime_load_from_file ... \n");
    am_allocator_pool_reset_vm(g_pool);
    am_allocator_pool_reset_heap(g_pool);
    test_halt_called = 0;
    test_error_called = 0;

    char *base_dir = am_path_dirname((char*)path);

    wchar_t *path_w = (wchar_t *)am_malloc(vm_alloc, 256 * sizeof(wchar_t));
    wchar_t *base_dir_w = (wchar_t *)am_malloc(vm_alloc, 256 * sizeof(wchar_t));

    _mbstowcs(path_w, path, 256);
    _mbstowcs(base_dir_w, base_dir, 256);

    wchar_t *file_content = read_file_as_wstring(path_w);
    assert(file_content != NULL);

    const wchar_t *prefix = L"((lambda () \n";
    const wchar_t *suffix = L"\n))";
    size_t prefix_len = wcslen(prefix);
    size_t suffix_len = wcslen(suffix);
    size_t content_len = wcslen(file_content);
    size_t code_len = prefix_len + content_len + suffix_len;

    wchar_t *code = (wchar_t *)am_malloc(vm_alloc, (code_len + 1) * sizeof(wchar_t));
    assert(code != NULL);
    size_t pos = 0;
    for (size_t i = 0; i < prefix_len; i++) code[pos++] = prefix[i];
    for (size_t i = 0; i < content_len; i++) code[pos++] = file_content[i];
    for (size_t i = 0; i < suffix_len; i++) code[pos++] = suffix[i];
    code[pos] = L'\0';
    free(file_content);

    am_ast_t *ast = am_parse(vm_alloc, code, path_w);
    assert(ast != NULL);

    // for (int32_t i = 0; i < ast->token_count; i++) {
    //     printf("[%4d] %12ls @%5zu+%3zu  %ls\n", 
    //         i, type_name(ast->tokens[i].type), 
    //         ast->tokens[i].index, ast->tokens[i].length,
    //         token_text(&(ast->tokens)[i], code));
    // }

    // printf("\033[1m=== 语法高亮输出 ===\033[0m\n");
    // am_print_highlighted(code, ast->tokens, ast->token_count);

    am_ast_t *linked = am_link(ast, base_dir_w);
    assert(linked != NULL);

    // printf("=== AST ===\n");
    // am_debug_ast_print_to_stdout(linked);


    am_heap_t *nodes_copy = am_heap_copy(vm_alloc, vm_alloc, linked->nodes);
    assert(nodes_copy != NULL);

    size_t dump_size = am_heap_deep_dump(vm_alloc, vm_alloc, nodes_copy, NULL, 0);
    assert(dump_size != SIZE_MAX);

    uint8_t *dump_buffer = (uint8_t *)malloc(dump_size);
    assert(dump_buffer);
    memset(dump_buffer, 0, dump_size);

    size_t written = am_heap_deep_dump(vm_alloc, vm_alloc, nodes_copy, dump_buffer, 0);
    assert(written == dump_size);

    am_heap_t *loaded_nodes = am_heap_deep_load(vm_alloc, vm_alloc, dump_buffer, 0);
    assert(loaded_nodes != NULL);

    am_ast_t loaded_linked_ast = *linked;
    loaded_linked_ast.nodes = loaded_nodes;


    am_module_t *mod = am_compile(&loaded_linked_ast);
    assert(mod != NULL);
    printf("OPSTACK depth = %zu\n", mod->opstack_depth);

    // printf("=== IL Code ===\n");
    // am_debug_print_ilcode(mod->ast, mod->ilcode, mod->ilcode_length);

    am_runtime_t *rt = am_runtime_create(vm_alloc, heap_alloc, base_dir_w);
    assert(rt != NULL);

    // 注册内置 native 库
    am_runtime_register_native_lib(rt, &am_native_System_lib);
    am_runtime_register_native_lib(rt, &am_native_Math_lib);
    am_runtime_register_native_lib(rt, &am_native_String_lib);

    rt->callback_on_halt = on_halt;
    rt->callback_on_error = on_error;

    am_pid_t pid1 = am_load_module(rt, mod);
    (void)pid1;
    // am_pid_t pid2 = am_load_module(rt, mod);

    printf("=== VM output ===\n");
    am_start(rt);
    printf("\n=== VM halted ===\n");

    am_runtime_destroy(rt);
    free(dump_buffer);
    free(base_dir);
    free(mod->ilcode);
    am_free(linked->alloc, mod);
    am_ast_destroy(linked);
}


int main(int argc, char* argv[]) {




    if (!setlocale(LC_ALL, "")) {
        fprintf(stderr, "setlocale failed\n");
        return 1;
    }

    // 读取源码文件（支持UTF-8）
    if(argc < 2) {
        printf("Usage: %s <source.scm>\n", argv[0]);
        return 1;
    }

    g_pool = am_allocator_pool_create(AM_ALLOCATOR_POOL_SIZE);
    if (!g_pool) {
        fprintf(stderr, "Failed to create allocator pool\n");
        return 1;
    }
    vm_alloc = am_allocator_pool_get_vm(g_pool);
    heap_alloc = am_allocator_pool_get_heap(g_pool);

    test_runtime_load_from_file(argv[1]);

    am_allocator_pool_destroy(g_pool);
    g_pool = NULL;
    vm_alloc = NULL;
    heap_alloc = NULL;

    return 0;
}
