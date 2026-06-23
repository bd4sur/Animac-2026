#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wchar.h>
#include <locale.h>

#include "linker.h"
#include "parser.h"
#include "ast.h"
#include "object.h"
#include "list.h"
#include "vocab.h"
#include "wstring.h"
#include "map.h"
#include "heap.h"
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

static void test_allocator_reset(void) {
    test_allocator_state.offset = 0;
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
// 测试辅助函数
// ===============================================================================

static am_list_t *handle_to_list(am_ast_t *ast, am_handle_t handle) {
    am_value_t v = am_ast_get_node(ast, handle);
    assert(am_value_is_ptr(v));
    return (am_list_t *)am_value_to_ptr(v);
}

static am_wstring_t *handle_to_wstring(am_ast_t *ast, am_handle_t handle) {
    am_value_t v = am_ast_get_node(ast, handle);
    assert(am_value_is_ptr(v));
    return (am_wstring_t *)am_value_to_ptr(v);
}

static wchar_t *wstring_to_buf(am_wstring_t *ws) {
    size_t len = ws->length;
    wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    assert(buf != NULL);
    for (size_t i = 0; i < len; i++) {
        buf[i] = (wchar_t)am_value_to_wchar(ws->content[i]);
    }
    buf[len] = L'\0';
    return buf;
}

static am_varid_t varid_of(am_ast_t *ast, const wchar_t *name) {
    size_t idx = am_vocab_find(ast->alloc, ast->var_vocab, (wchar_t *)name);
    assert(idx != SIZE_MAX);
    return (am_varid_t)idx;
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
// 链接器测试
// ===============================================================================

static void test_linker_no_dependencies(void) {
    printf("test_linker_no_dependencies ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () (define x 42) x))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_ast_t *linked = am_link(ast, L"/");
    assert(linked != NULL);
    assert(linked == ast);

    // 无依赖时 module_id 与 var_top 保持不变
    assert(wcscmp(linked->module_id, L"test") == 0);
    assert(linked->var_top->length == 1);

    am_ast_destroy(linked);
    printf("OK\n");
}


static void test_linker_recursive(void) {
    printf("test_linker_recursive ... \n");
    test_allocator_reset();

    const wchar_t *path = L"/home/bd4sur/animac/x.scm";
    const wchar_t *base_dir = L"/home/bd4sur/animac";

    wchar_t *file_content = read_file_as_wstring(path);
    assert(file_content != NULL);

    const wchar_t *prefix = L"((lambda () ";
    const wchar_t *suffix = L" ))";
    size_t prefix_len = wcslen(prefix);
    size_t suffix_len = wcslen(suffix);
    size_t content_len = wcslen(file_content);
    size_t code_len = prefix_len + content_len + suffix_len;

    wchar_t *code = (wchar_t *)am_malloc(&test_allocator, (code_len + 1) * sizeof(wchar_t));
    assert(code != NULL);
    size_t pos = 0;
    for (size_t i = 0; i < prefix_len; i++) code[pos++] = prefix[i];
    for (size_t i = 0; i < content_len; i++) code[pos++] = file_content[i];
    for (size_t i = 0; i < suffix_len; i++) code[pos++] = suffix[i];
    code[pos] = L'\0';
    free(file_content);

    am_ast_t *ast = am_parser(&test_allocator, code, (wchar_t *)path);
    assert(ast != NULL);

    am_handle_t importer_top_lambda = ast->top_lambda_handle;
    assert(importer_top_lambda != AM_HANDLE_NULL);

    // 执行链接
    am_ast_t *linked = am_link(ast, (wchar_t *)base_dir);
    assert(linked != NULL);
    assert(linked == ast);

    // 执行外部引用解析
    int32_t resolution_result = am_linker_import_ref_resolution(NULL, linked);
    assert(resolution_result == 0);

    // 可视化输出解析后的 AST
    printf("=== resolved AST ===\n");
    am_debug_ast_print_to_stdout(linked);

    // 将 ast->nodes 深度转储后再加载，验证 dump/load 正确性
    printf("=== dump/load nodes ===\n");
    am_heap_t *nodes_copy = am_heap_copy(&test_allocator, linked->nodes);
    assert(nodes_copy != NULL);

    size_t dump_size = am_heap_deep_dump(&test_allocator, nodes_copy, NULL, 0);
    assert(dump_size != SIZE_MAX);
    printf("deep dump size: %zu\n", dump_size);

    uint8_t *dump_buffer = (uint8_t *)malloc(dump_size);
    assert(dump_buffer != NULL);
    memset(dump_buffer, 0, dump_size);

    size_t written = am_heap_deep_dump(&test_allocator, nodes_copy, dump_buffer, 0);
    assert(written == dump_size);

    am_heap_t *loaded_nodes = am_heap_deep_load(&test_allocator, dump_buffer, 0);
    assert(loaded_nodes != NULL);

    // 用原 AST 的元数据构造一个临时 AST，仅替换 nodes 为加载后的 heap，
    // 然后通过 AST 打印函数输出，以验证 dump/load 后节点内容一致。
    am_ast_t loaded_ast = *linked;
    loaded_ast.nodes = loaded_nodes;

    printf("=== loaded AST (from dumped nodes) ===\n");
    am_debug_ast_print_to_stdout(&loaded_ast);

    // 将加载后的 AST 根节点转回 Scheme 代码并打印
    am_handle_t loaded_top = am_ast_get_top_node_handle(&loaded_ast);
    assert(loaded_top != AM_HANDLE_NULL);
    size_t root_code_len = 0;
    wchar_t *code_str = am_ast_node_to_string(&test_allocator, &loaded_ast, loaded_top, &root_code_len);
    assert(code_str != NULL);
    printf("=== loaded AST root as Scheme code (length=%zu) ===\n", root_code_len);
    printf("%ls\n", code_str);

    free(dump_buffer);
    // nodes_copy 在深dump后 table value 已变为偏移量，不再有效，故不销毁
    // loaded_nodes 可由测试分配器统一回收

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

    test_allocator_init();

    // test_linker_no_dependencies();
    test_linker_recursive();

    test_destroy(&test_allocator_state);

    printf("\nAll linker tests passed.\n");
    return 0;
}
