#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wchar.h>
#include <locale.h>

#include "parser.h"
#include "ast.h"
#include "object.h"
#include "list.h"
#include "vocab.h"
#include "wstring.h"
#include "lexer.h"


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

// 获取 Alpha-renaming 后的变量 varid（不创建新条目，仅查找）
static am_varid_t arnid_of(am_ast_t *ast, am_handle_t lambda_handle, const wchar_t *name) {
    wchar_t buf[256];
    int n = swprintf(buf, 256, L"%ls.%zu.%ls", ast->module_id, lambda_handle, name);
    assert(n > 0 && n < 256);
    size_t idx = am_vocab_find(ast->alloc, ast->var_vocab, buf);
    assert(idx != SIZE_MAX);
    return (am_varid_t)idx;
}

static am_symbol_t symbol_of(am_ast_t *ast, const wchar_t *name) {
    size_t idx = am_vocab_find(ast->alloc, ast->symbol_vocab, (wchar_t *)name);
    assert(idx != SIZE_MAX);
    return (am_symbol_t)idx;
}


// ===============================================================================
// Token 序列打印（参照 main.c 格式）
// ===============================================================================

static const wchar_t *type_name(int32_t type) {
    switch (type) {
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

static void print_tokens(wchar_t *code, am_token_t *tokens, size_t count) {
    for (size_t i = 0; i < count; i++) {
        printf("[%4zu] %12ls @%5zu+%3zu  %ls\n",
            i, type_name(tokens[i].type),
            tokens[i].index, tokens[i].length,
            token_text(&tokens[i], code));
    }
}


// ===============================================================================
// 测试用例
// ===============================================================================

static void test_parse_empty_arglist(void) {
    printf("test_parse_empty_arglist ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () #null))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_app = am_ast_get_top_node_handle(ast);
    assert(top_app != AM_HANDLE_NULL);
    am_list_t *app = handle_to_list(ast, top_app);
    assert(app->type == AM_LIST_TYPE_APPLICATION);
    assert(app->length == 1);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    assert(top_lambda != AM_HANDLE_NULL);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(lambda->type == AM_LIST_TYPE_LAMBDA);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 1);

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_number_literal(void) {
    printf("test_parse_number_literal ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () 42))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 1);

    am_value_t body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_uint(body));
    assert(am_value_to_uint(body) == 42);

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_negative_number(void) {
    printf("test_parse_negative_number ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () -3.14))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 1);

    am_value_t body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_float(body));
    double d = (double)am_value_to_float(body);
    assert(d > -3.15 && d < -3.13);

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_string_literal(void) {
    printf("test_parse_string_literal ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () \"hello\"))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 1);

    am_value_t body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_handle(body));

    am_wstring_t *ws = handle_to_wstring(ast, am_value_to_handle(body));
    wchar_t *buf = wstring_to_buf(ws);
    assert(wcscmp(buf, L"\"hello\"") == 0);
    free(buf);

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_application(void) {
    printf("test_parse_application ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () (display x)))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 1);

    am_value_t body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_handle(body));

    am_list_t *app = handle_to_list(ast, am_value_to_handle(body));
    assert(app->type == AM_LIST_TYPE_APPLICATION);
    assert(app->length == 2);

    am_value_t op = am_list_get(ast->alloc, app, 0);
    assert(am_value_is_varid(op));
    // display 是全局内置变量，保持原始 varid
    assert(am_value_to_varid(op) == varid_of(ast, L"display"));

    am_value_t arg = am_list_get(ast->alloc, app, 1);
    assert(am_value_is_varid(arg));
    assert(am_value_to_varid(arg) == arnid_of(ast, top_lambda, L"x"));

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_lambda(void) {
    printf("test_parse_lambda ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () (lambda (y) y)))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 1);

    am_value_t body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_handle(body));

    am_list_t *inner = handle_to_list(ast, am_value_to_handle(body));
    assert(inner->type == AM_LIST_TYPE_LAMBDA);
    assert(inner->length == 4); // 'lambda, n_param, 1 param, 1 body

    am_value_t n_param = am_list_get(ast->alloc, inner, 1);
    assert(am_value_is_uint(n_param));
    assert(am_value_to_uint(n_param) == 1);

    am_value_t param = am_list_get(ast->alloc, inner, 2);
    assert(am_value_is_varid(param));
    assert(am_value_to_varid(param) == arnid_of(ast, am_value_to_handle(body), L"y"));

    am_value_t inner_body = am_list_get(ast->alloc, inner, 3);
    assert(am_value_is_varid(inner_body));
    assert(am_value_to_varid(inner_body) == arnid_of(ast, am_value_to_handle(body), L"y"));

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_quote_shorthand(void) {
    printf("test_parse_quote_shorthand ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () 'x))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 1);

    am_value_t body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_symbol(body));
    assert(am_value_to_symbol(body) == symbol_of(ast, L"'x"));

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_quote_list(void) {
    printf("test_parse_quote_list ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () '(a b)))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 1);

    am_value_t body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_handle(body));

    am_list_t *quote = handle_to_list(ast, am_value_to_handle(body));
    assert(quote->type == AM_LIST_TYPE_QUOTE);
    assert(quote->length == 2);

    am_value_t a = am_list_get(ast->alloc, quote, 0);
    assert(am_value_is_symbol(a));
    assert(am_value_to_symbol(a) == symbol_of(ast, L"'a"));

    am_value_t b = am_list_get(ast->alloc, quote, 1);
    assert(am_value_is_symbol(b));
    assert(am_value_to_symbol(b) == symbol_of(ast, L"'b"));

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_quote_keyword(void) {
    printf("test_parse_quote_keyword ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () 'lambda))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    am_value_t body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_symbol(body));
    assert(am_value_to_symbol(body) == symbol_of(ast, L"'lambda"));

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_unquote_in_quasiquote(void) {
    printf("test_parse_unquote_in_quasiquote ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () `(,x)))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 1);

    am_value_t body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_handle(body));

    am_list_t *qq = handle_to_list(ast, am_value_to_handle(body));
    assert(qq->type == AM_LIST_TYPE_QUASIQUOTE);
    assert(qq->length == 1);

    // 在 quasiquote 列表中的 ,x 直接解析为变量 x（不创建 UNQUOTE 列表）
    am_value_t child = am_list_get(ast->alloc, qq, 0);
    assert(am_value_is_varid(child));
    assert(am_value_to_varid(child) == varid_of(ast, L"x"));

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_import_native(void) {
    printf("test_parse_import_native ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () (import m \"path/to/m.scm\") (native N)))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    // import 别名和 native 模块名保持原形，不参与 Alpha-renaming
    am_varid_t m_varid = varid_of(ast, L"m");
    am_varid_t n_varid = varid_of(ast, L"N");

    // 验证 var_type 为 AM_VAR_TYPE_OLD
    am_value_t m_type = am_list_get(ast->alloc, ast->var_type, (size_t)m_varid);
    am_value_t n_type = am_list_get(ast->alloc, ast->var_type, (size_t)n_varid);
    assert(am_value_is_uint(m_type) && am_value_to_uint(m_type) == AM_VAR_TYPE_OLD);
    assert(am_value_is_uint(n_type) && am_value_to_uint(n_type) == AM_VAR_TYPE_OLD);

    // 旧 alias 的依赖映射已被删除
    am_value_t old_dep = am_map_get(ast->alloc, ast->dependencies, am_make_value_of_varid(m_varid));
    assert(!am_value_is_handle(old_dep));

    // 新 alias（module_id.m）的依赖映射存在
    am_varid_t new_m_varid = varid_of(ast, L"test.m");
    am_value_t new_dep = am_map_get(ast->alloc, ast->dependencies, am_make_value_of_varid(new_m_varid));
    assert(am_value_is_handle(new_dep));

    am_wstring_t *ws = handle_to_wstring(ast, am_value_to_handle(new_dep));
    wchar_t *buf = wstring_to_buf(ws);
    assert(wcscmp(buf, L"\"path/to/m.scm\"") == 0);
    free(buf);

    am_value_t nat = am_map_get(ast->alloc, ast->natives, am_make_value_of_varid(n_varid));
    assert(nat == AM_VALUE_HANDLE_NULL);

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_alpha_renaming(void) {
    printf("test_parse_alpha_renaming ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () (display x)))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    // 原始变量名仍然保留在词汇表中
    am_varid_t raw_display = varid_of(ast, L"display");
    am_varid_t raw_x = varid_of(ast, L"x");
    (void)raw_display;
    (void)raw_x;

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 1);

    am_value_t body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_handle(body));

    am_list_t *app = handle_to_list(ast, am_value_to_handle(body));
    assert(app->type == AM_LIST_TYPE_APPLICATION);
    assert(app->length == 2);

    // display 是全局内置变量，不做 Alpha-renaming；x 是普通变量，已被 Alpha-renaming
    am_value_t op = am_list_get(ast->alloc, app, 0);
    assert(am_value_is_varid(op));
    assert(am_value_to_varid(op) == varid_of(ast, L"display"));

    am_value_t arg = am_list_get(ast->alloc, app, 1);
    assert(am_value_is_varid(arg));
    assert(am_value_to_varid(arg) == arnid_of(ast, top_lambda, L"x"));

    // ARN 后的名字与原始名字不同（仅对非内置变量）
    assert(am_value_to_varid(arg) != varid_of(ast, L"x"));

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_alpha_renaming_nested(void) {
    printf("test_parse_alpha_renaming_nested ... ");
    test_allocator_reset();

    // 外层 lambda 中有同名变量 x 的引用；内层 lambda 将 x 作为参数并在体中引用。
    // 二者应被换名为不同的 varid。
    wchar_t *code = L"((lambda () (lambda (x) x) (f x)))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *top = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, top) == 2);

    // 第一个 body 是内层 lambda
    am_value_t inner_lambda_val = am_list_get(ast->alloc, top, 2);
    assert(am_value_is_handle(inner_lambda_val));
    am_handle_t inner_lambda = am_value_to_handle(inner_lambda_val);
    am_list_t *inner = handle_to_list(ast, inner_lambda);
    assert(inner->type == AM_LIST_TYPE_LAMBDA);
    am_value_t inner_n_param = am_list_get(ast->alloc, inner, 1);
    assert(am_value_is_uint(inner_n_param));
    assert(am_value_to_uint(inner_n_param) == 1);
    assert(am_list_lambda_get_body_number(ast->alloc, inner) == 1);

    // 内层 lambda 的参数 x 与体中的 x 引用应具有相同的 ARN（同一作用域）
    am_value_t inner_param = am_list_get(ast->alloc, inner, 2);
    am_value_t inner_body = am_list_get(ast->alloc, inner, 3);
    assert(am_value_is_varid(inner_param));
    assert(am_value_is_varid(inner_body));
    assert(am_value_to_varid(inner_param) == am_value_to_varid(inner_body));
    assert(am_value_to_varid(inner_param) == arnid_of(ast, inner_lambda, L"x"));

    // 第二个 body 是外层应用 (f x)
    am_value_t outer_app_val = am_list_get(ast->alloc, top, 3);
    assert(am_value_is_handle(outer_app_val));
    am_list_t *outer_app = handle_to_list(ast, am_value_to_handle(outer_app_val));
    assert(outer_app->type == AM_LIST_TYPE_APPLICATION);
    assert(outer_app->length == 2);

    am_value_t outer_f = am_list_get(ast->alloc, outer_app, 0);
    am_value_t outer_x = am_list_get(ast->alloc, outer_app, 1);
    assert(am_value_is_varid(outer_f));
    assert(am_value_is_varid(outer_x));
    assert(am_value_to_varid(outer_f) == arnid_of(ast, top_lambda, L"f"));
    assert(am_value_to_varid(outer_x) == arnid_of(ast, top_lambda, L"x"));

    // 外层 x 与内层 x 的 ARN 不同
    assert(am_value_to_varid(outer_x) != am_value_to_varid(inner_param));

    am_ast_destroy(ast);
    printf("OK\n");
}


// ===============================================================================
// AST 可视化（JSON-like 树状格式）
// ===============================================================================

#define AST_PRINT_INDENT 2

static void ast_print_indent(FILE *out, int depth) {
    for (int i = 0; i < depth * AST_PRINT_INDENT; i++) {
        fputwc(L' ', out);
    }
}

static int ast_is_handle_visited(am_handle_t *visited, size_t count, am_handle_t handle) {
    for (size_t i = 0; i < count; i++) {
        if (visited[i] == handle) return 1;
    }
    return 0;
}

static void ast_print_value(am_ast_t *ast, am_value_t value, FILE *out,
                            am_handle_t *visited, size_t *visited_count, int depth);

static void ast_print_node(am_ast_t *ast, am_handle_t handle, FILE *out,
                           am_handle_t *visited, size_t *visited_count, int depth) {
    am_value_t node_val = am_ast_get_node(ast, handle);
    if (!am_value_is_ptr(node_val)) {
        fwprintf(out, L"<H:%zu> (invalid)", (size_t)handle);
        return;
    }

    am_object_t *obj = am_value_to_ptr(node_val);

    if (obj->type == AM_OBJECT_TYPE_WSTRING) {
        am_wstring_t *ws = (am_wstring_t *)obj;
        fputwc(L'"', out);
        for (size_t i = 0; i < ws->length; i++) {
            wchar_t c = (wchar_t)am_value_to_wchar(ws->content[i]);
            if (c == L'"') fputwc(L'\\', out);
            fwprintf(out, L"%lc", c);
        }
        fputwc(L'"', out);
        return;
    }

    if (obj->type != AM_OBJECT_TYPE_LIST) {
        fwprintf(out, L"<H:%zu> (unknown type %d)", (size_t)handle, obj->type);
        return;
    }

    am_list_t *lst = (am_list_t *)obj;

    if (ast_is_handle_visited(visited, *visited_count, handle)) {
        fwprintf(out, L"<H:%zu>", (size_t)handle);
        return;
    }
    visited[(*visited_count)++] = handle;

    fwprintf(out, L"<H:%zu> {\n", (size_t)handle);
    ast_print_indent(out, depth + 1);
    fwprintf(out, L"type: ");
    switch (lst->type) {
        case AM_LIST_TYPE_LAMBDA:      fwprintf(out, L"\"LAMBDA\"\n"); break;
        case AM_LIST_TYPE_APPLICATION: fwprintf(out, L"\"APPLICATION\"\n"); break;
        case AM_LIST_TYPE_QUOTE:       fwprintf(out, L"\"QUOTE\"\n"); break;
        case AM_LIST_TYPE_QUASIQUOTE:  fwprintf(out, L"\"QUASIQUOTE\"\n"); break;
        case AM_LIST_TYPE_UNQUOTE:     fwprintf(out, L"\"UNQUOTE\"\n"); break;
        default:                       fwprintf(out, L"\"UNKNOWN(%d)\"\n", lst->type); break;
    }

    ast_print_indent(out, depth + 1);
    if (lst->parent == AM_HANDLE_NULL) {
        fwprintf(out, L"parent: null\n");
    }
    else {
        fwprintf(out, L"parent: <H:%zu>\n", (size_t)lst->parent);
    }

    if (lst->type == AM_LIST_TYPE_LAMBDA) {
        am_uint_t n_param = 0;
        if (lst->length >= 2) {
            am_value_t np = am_list_get(ast->alloc, lst, 1);
            if (am_value_is_uint(np)) n_param = am_value_to_uint(np);
        }

        ast_print_indent(out, depth + 1);
        fwprintf(out, L"parameters: [\n");
        for (am_uint_t i = 0; i < n_param; i++) {
            ast_print_indent(out, depth + 2);
            am_value_t param = am_list_get(ast->alloc, lst, 2 + i);
            ast_print_value(ast, param, out, visited, visited_count, depth + 2);
            if (i + 1 < n_param) fputwc(L',', out);
            fputwc(L'\n', out);
        }
        ast_print_indent(out, depth + 1);
        fwprintf(out, L"]\n");

        ast_print_indent(out, depth + 1);
        fwprintf(out, L"bodies: [\n");
        size_t body_start = 2 + n_param;
        for (size_t i = body_start; i < lst->length; i++) {
            ast_print_indent(out, depth + 2);
            am_value_t body = am_list_get(ast->alloc, lst, i);
            ast_print_value(ast, body, out, visited, visited_count, depth + 2);
            if (i + 1 < lst->length) fputwc(L',', out);
            fputwc(L'\n', out);
        }
        ast_print_indent(out, depth + 1);
        fwprintf(out, L"]\n");
    }
    else {
        ast_print_indent(out, depth + 1);
        fwprintf(out, L"children: [\n");
        for (size_t i = 0; i < lst->length; i++) {
            ast_print_indent(out, depth + 2);
            am_value_t child = am_list_get(ast->alloc, lst, i);
            ast_print_value(ast, child, out, visited, visited_count, depth + 2);
            if (i + 1 < lst->length) fputwc(L',', out);
            fputwc(L'\n', out);
        }
        ast_print_indent(out, depth + 1);
        fwprintf(out, L"]\n");
    }

    ast_print_indent(out, depth);
    fwprintf(out, L"}");
}

static void ast_print_value(am_ast_t *ast, am_value_t value, FILE *out,
                            am_handle_t *visited, size_t *visited_count, int depth) {
    if (am_value_is_handle(value)) {
        am_handle_t h = am_value_to_handle(value);
        ast_print_node(ast, h, out, visited, visited_count, depth);
    }
    else if (am_value_is_varid(value)) {
        am_varid_t varid = am_value_to_varid(value);
        size_t idx = (size_t)varid;
        wchar_t *name = am_vocab_get(ast->alloc, ast->var_vocab, &idx);
        if (name) {
            fwprintf(out, L"\"%ls\" (varid=%zu)", name, (size_t)varid);
        }
        else {
            fwprintf(out, L"<varid=%zu>", (size_t)varid);
        }
    }
    else if (am_value_is_symbol(value)) {
        am_symbol_t sym = am_value_to_symbol(value);
        size_t idx = (size_t)sym;
        wchar_t *name = am_vocab_get(ast->alloc, ast->symbol_vocab, &idx);
        if (name) {
            fwprintf(out, L"\"%ls\" (symbol=%zu)", name, (size_t)sym);
        }
        else {
            fwprintf(out, L"<symbol=%zu>", (size_t)sym);
        }
    }
    else if (am_value_is_uint(value)) {
        fwprintf(out, L"%llu", (unsigned long long)am_value_to_uint(value));
    }
    else if (am_value_is_int(value)) {
        fwprintf(out, L"%lld", (long long)am_value_to_int(value));
    }
    else if (am_value_is_float(value)) {
        fwprintf(out, L"%g", (double)am_value_to_float(value));
    }
    else if (am_value_is_boolean(value)) {
        fwprintf(out, L"%ls", am_value_to_boolean(value) ? L"#t" : L"#f");
    }
    else if (am_value_is_null(value)) {
        fwprintf(out, L"#null");
    }
    else if (am_value_is_undefined(value)) {
        fwprintf(out, L"#undefined");
    }
    else {
        fwprintf(out, L"<value=%llu>", (unsigned long long)value);
    }
}

static void ast_print_vocab(FILE *out, am_ast_t *ast, am_vocab_t *vocab) {
    fputwc(L'[', out);
    for (size_t i = 0; i < vocab->length; i++) {
        size_t idx = i;
        wchar_t *word = am_vocab_get(ast->alloc, vocab, &idx);
        if (i > 0) fwprintf(out, L", ");
        if (word) {
            fputwc(L'"', out);
            fwprintf(out, L"%ls", word);
            fputwc(L'"', out);
        }
        else {
            fwprintf(out, L"null");
        }
    }
    fputwc(L']', out);
}

static void ast_print_handle_list(FILE *out, am_ast_t *ast, am_list_t *lst) {
    (void)ast;
    fputwc(L'[', out);
    for (size_t i = 0; i < lst->length; i++) {
        if (i > 0) fwprintf(out, L", ");
        fwprintf(out, L"<H:%zu>", (size_t)am_value_to_handle(am_list_get(ast->alloc, lst, i)));
    }
    fputwc(L']', out);
}


static void ast_print_value_inline(am_ast_t *ast, am_value_t value, FILE *out);


static void ast_print_value_list(FILE *out, am_ast_t *ast, am_list_t *lst) {
    fputwc(L'[', out);
    for (size_t i = 0; i < lst->length; i++) {
        if (i > 0) fwprintf(out, L", ");
        ast_print_value_inline(ast, am_list_get(ast->alloc, lst, i), out);
    }
    fputwc(L']', out);
}

static void ast_print_map_varid_to_handle(FILE *out, am_ast_t *ast, am_map_t *map) {
    fputwc(L'{', out);
    size_t count = am_map_length(ast->alloc, map);
    am_value_t *keys = am_map_keys(ast->alloc, map);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) fwprintf(out, L", ");
        am_varid_t varid = am_value_to_varid(keys[i]);
        size_t idx = (size_t)varid;
        wchar_t *name = am_vocab_get(ast->alloc, ast->var_vocab, &idx);
        if (name) fwprintf(out, L"\"%ls\": ", name);
        else fwprintf(out, L"<varid=%zu>: ", (size_t)varid);

        am_value_t v = am_map_get(ast->alloc, map, keys[i]);
        if (am_value_is_handle(v)) {
            fwprintf(out, L"<H:%zu>", (size_t)am_value_to_handle(v));
        }
        else {
            fwprintf(out, L"null");
        }
    }
    if (keys) free(keys);
    fputwc(L'}', out);
}

static const wchar_t *var_type_name(am_uint_t type) {
    switch (type) {
        case AM_VAR_TYPE_OLD:          return L"OLD";
        case AM_VAR_TYPE_NEW:          return L"NEW";
        case AM_VAR_TYPE_BUILTIN:      return L"BUILTIN";
        case AM_VAR_TYPE_EXT_REF:      return L"EXT_REF";
        case AM_VAR_TYPE_IMPORT_REF:   return L"IMPORT_REF";
        case AM_VAR_TYPE_NATIVE_REF:   return L"NATIVE_REF";
        case AM_VAR_TYPE_IMPORT_ALIAS: return L"IMPORT_ALIAS";
        case AM_VAR_TYPE_NATIVE_ID:    return L"NATIVE_ID";
        default:                       return L"UNKNOWN";
    }
}

static void ast_print_var_type(FILE *out, am_ast_t *ast, am_list_t *lst) {
    (void)ast;
    fputwc(L'[', out);
    for (size_t i = 0; i < lst->length; i++) {
        if (i > 0) fwprintf(out, L", ");
        am_value_t v = am_list_get(ast->alloc, lst, i);
        if (am_value_is_uint(v)) {
            am_uint_t t = am_value_to_uint(v);
            fwprintf(out, L"%ls(%zu)", var_type_name(t), (size_t)t);
        }
        else {
            fwprintf(out, L"?");
        }
    }
    fputwc(L']', out);
}

static void ast_print_var_arn_mapping(FILE *out, am_ast_t *ast, am_map_t *map) {
    fputwc(L'{', out);
    size_t count = am_map_length(ast->alloc, map);
    am_value_t *keys = am_map_keys(ast->alloc, map);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) fwprintf(out, L", ");
        am_varid_t new_varid = am_value_to_varid(keys[i]);
        size_t new_idx = (size_t)new_varid;
        wchar_t *new_name = am_vocab_get(ast->alloc, ast->var_vocab, &new_idx);
        if (new_name) fwprintf(out, L"\"%ls\": ", new_name);
        else fwprintf(out, L"<varid=%zu>: ", (size_t)new_varid);

        am_value_t v = am_map_get(ast->alloc, map, keys[i]);
        if (am_value_is_varid(v)) {
            am_varid_t old_varid = am_value_to_varid(v);
            size_t old_idx = (size_t)old_varid;
            wchar_t *old_name = am_vocab_get(ast->alloc, ast->var_vocab, &old_idx);
            if (old_name) fwprintf(out, L"\"%ls\"", old_name);
            else fwprintf(out, L"<varid=%zu>", (size_t)old_varid);
        }
        else {
            fwprintf(out, L"null");
        }
    }
    if (keys) free(keys);
    fputwc(L'}', out);
}

static void ast_print_value_inline(am_ast_t *ast, am_value_t value, FILE *out) {
    if (am_value_is_handle(value)) {
        fwprintf(out, L"<H:%zu>", (size_t)am_value_to_handle(value));
    }
    else if (am_value_is_varid(value)) {
        am_varid_t varid = am_value_to_varid(value);
        size_t idx = (size_t)varid;
        wchar_t *name = am_vocab_get(ast->alloc, ast->var_vocab, &idx);
        if (name) fwprintf(out, L"\"%ls\"", name);
        else fwprintf(out, L"<varid=%zu>", (size_t)varid);
    }
    else if (am_value_is_symbol(value)) {
        am_symbol_t sym = am_value_to_symbol(value);
        size_t idx = (size_t)sym;
        wchar_t *name = am_vocab_get(ast->alloc, ast->symbol_vocab, &idx);
        if (name) fwprintf(out, L"\"%ls\"", name);
        else fwprintf(out, L"<symbol=%zu>", (size_t)sym);
    }
    else if (am_value_is_uint(value)) {
        fwprintf(out, L"%llu", (unsigned long long)am_value_to_uint(value));
    }
    else if (am_value_is_int(value)) {
        fwprintf(out, L"%lld", (long long)am_value_to_int(value));
    }
    else if (am_value_is_float(value)) {
        fwprintf(out, L"%g", (double)am_value_to_float(value));
    }
    else if (am_value_is_boolean(value)) {
        fwprintf(out, L"%ls", am_value_to_boolean(value) ? L"#t" : L"#f");
    }
    else if (am_value_is_null(value)) {
        fwprintf(out, L"#null");
    }
    else if (am_value_is_undefined(value)) {
        fwprintf(out, L"#undefined");
    }
    else {
        fwprintf(out, L"<value=%llu>", (unsigned long long)value);
    }
}


static void ast_print_node_summary(am_ast_t *ast, am_handle_t handle, FILE *out) {
    am_value_t node_val = am_ast_get_node(ast, handle);
    if (!am_value_is_ptr(node_val)) {
        fwprintf(out, L"<H:%zu>: (invalid)\n", (size_t)handle);
        return;
    }

    am_object_t *obj = am_value_to_ptr(node_val);
    if (obj->type == AM_OBJECT_TYPE_WSTRING) {
        am_wstring_t *ws = (am_wstring_t *)obj;
        fwprintf(out, L"<H:%zu>: WSTRING len=%zu \"", (size_t)handle, ws->length);
        for (size_t i = 0; i < ws->length; i++) {
            wchar_t c = (wchar_t)am_value_to_wchar(ws->content[i]);
            if (c == L'"') fputwc(L'\\', out);
            fwprintf(out, L"%lc", c);
        }
        fwprintf(out, L"\"\n");
        return;
    }

    if (obj->type != AM_OBJECT_TYPE_LIST) {
        fwprintf(out, L"<H:%zu>: (unknown type %d)\n", (size_t)handle, obj->type);
        return;
    }

    am_list_t *lst = (am_list_t *)obj;
    const wchar_t *type_name = L"UNKNOWN";
    switch (lst->type) {
        case AM_LIST_TYPE_LAMBDA:      type_name = L"LAMBDA"; break;
        case AM_LIST_TYPE_APPLICATION: type_name = L"APPLICATION"; break;
        case AM_LIST_TYPE_QUOTE:       type_name = L"QUOTE"; break;
        case AM_LIST_TYPE_QUASIQUOTE:  type_name = L"QUASIQUOTE"; break;
        case AM_LIST_TYPE_UNQUOTE:     type_name = L"UNQUOTE"; break;
    }

    fwprintf(out, L"<H:%zu>: %ls parent=", (size_t)handle, type_name);
    if (lst->parent == AM_HANDLE_NULL) {
        fwprintf(out, L"null");
    }
    else {
        fwprintf(out, L"<H:%zu>", (size_t)lst->parent);
    }

    if (lst->type == AM_LIST_TYPE_LAMBDA) {
        am_uint_t n_param = 0;
        if (lst->length >= 2) {
            am_value_t np = am_list_get(ast->alloc, lst, 1);
            if (am_value_is_uint(np)) n_param = am_value_to_uint(np);
        }
        size_t n_body = (lst->length > 2 + n_param) ? (lst->length - 2 - n_param) : 0;
        fwprintf(out, L" params=%u bodies=%zu", (unsigned)n_param, n_body);
    }
    else {
        fwprintf(out, L" length=%zu", lst->length);
    }

    fwprintf(out, L" children=[");
    for (size_t i = 0; i < lst->length; i++) {
        if (i > 0) fwprintf(out, L", ");
        ast_print_value_inline(ast, am_list_get(ast->alloc, lst, i), out);
    }
    fwprintf(out, L"]\n");
}


typedef struct {
    am_ast_t *ast;
    FILE *out;
} ast_print_nodes_ctx_t;

static void ast_print_nodes_iter_cb(am_handle_t handle, am_value_t value, void *user_data) {
    (void)value;
    ast_print_nodes_ctx_t *ctx = (ast_print_nodes_ctx_t *)user_data;
    ast_print_indent(ctx->out, 2);
    ast_print_node_summary(ctx->ast, handle, ctx->out);
}


static void ast_print_nodes_map(FILE *out, am_ast_t *ast) {
    ast_print_indent(out, 1);
    fwprintf(out, L"nodes: {\n");
    ast_print_nodes_ctx_t ctx = { ast, out };
    am_heap_iter(ast->alloc, ast->nodes, ast_print_nodes_iter_cb, &ctx);
    ast_print_indent(out, 1);
    fwprintf(out, L"}\n");
}


static void ast_print(FILE *out, am_ast_t *ast) {
    if (!ast) {
        fwprintf(out, L"null\n");
        return;
    }

    am_handle_t visited[256];
    size_t visited_count = 0;

    fwprintf(out, L"AST {\n");
    ast_print_indent(out, 1);
    fwprintf(out, L"absolute_path: \"%ls\"\n", ast->absolute_path ? ast->absolute_path : L"(null)");
    ast_print_indent(out, 1);
    fwprintf(out, L"module_id: \"%ls\"\n", ast->module_id ? ast->module_id : L"(null)");
    ast_print_indent(out, 1);
    fwprintf(out, L"top_lambda_handle: <H:%zu>\n", (size_t)ast->top_lambda_handle);
    ast_print_indent(out, 1);
    fwprintf(out, L"token_count: %zu\n", ast->token_count);
    ast_print_indent(out, 1);
    fwprintf(out, L"symbol_vocab: ");
    ast_print_vocab(out, ast, ast->symbol_vocab);
    fputwc(L'\n', out);
    ast_print_indent(out, 1);
    fwprintf(out, L"var_vocab: ");
    ast_print_vocab(out, ast, ast->var_vocab);
    fputwc(L'\n', out);
    ast_print_indent(out, 1);
    fwprintf(out, L"var_type: ");
    ast_print_var_type(out, ast, ast->var_type);
    fputwc(L'\n', out);
    ast_print_indent(out, 1);
    fwprintf(out, L"var_arn_mapping: ");
    ast_print_var_arn_mapping(out, ast, ast->var_arn_mapping);
    fputwc(L'\n', out);
    ast_print_indent(out, 1);
    fwprintf(out, L"lambda_handles: ");
    ast_print_handle_list(out, ast, ast->lambda_handles);
    fputwc(L'\n', out);
    ast_print_indent(out, 1);
    fwprintf(out, L"tailcall_handles: ");
    ast_print_handle_list(out, ast, ast->tailcall_handles);
    fputwc(L'\n', out);
    ast_print_indent(out, 1);
    fwprintf(out, L"var_top: ");
    ast_print_value_list(out, ast, ast->var_top);
    fputwc(L'\n', out);
    ast_print_indent(out, 1);
    fwprintf(out, L"dependencies: ");
    ast_print_map_varid_to_handle(out, ast, ast->dependencies);
    fputwc(L'\n', out);
    ast_print_indent(out, 1);
    fwprintf(out, L"natives: ");
    ast_print_map_varid_to_handle(out, ast, ast->natives);
    fputwc(L'\n', out);

    ast_print_nodes_map(out, ast);

    ast_print_indent(out, 1);
    fwprintf(out, L"top_node: ");
    am_handle_t top = am_ast_get_top_node_handle(ast);
    if (top == AM_HANDLE_NULL) {
        fwprintf(out, L"null\n");
    }
    else {
        fputwc(L'\n', out);
        ast_print_node(ast, top, out, visited, &visited_count, 2);
        fputwc(L'\n', out);
    }

    fwprintf(out, L"}\n");
}


static void test_parse_import_alias_rename(void) {
    printf("test_parse_import_alias_rename ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () (import Lib \"path/to/lib.scm\") (Lib.foo x)))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/path/to/a.scm");
    assert(ast != NULL);

    // module_id 应为 path.to.a
    assert(wcscmp(ast->module_id, L"path.to.a") == 0);

    // 旧别名和旧外部引用仍保留
    am_varid_t old_alias = varid_of(ast, L"Lib");
    am_varid_t old_ref = varid_of(ast, L"Lib.foo");

    am_value_t old_alias_type = am_list_get(ast->alloc, ast->var_type, (size_t)old_alias);
    am_value_t old_ref_type = am_list_get(ast->alloc, ast->var_type, (size_t)old_ref);
    assert(am_value_is_uint(old_alias_type) && am_value_to_uint(old_alias_type) == AM_VAR_TYPE_OLD);
    assert(am_value_is_uint(old_ref_type) && am_value_to_uint(old_ref_type) == AM_VAR_TYPE_EXT_REF);

    // 新别名和新外部引用
    am_varid_t new_alias = varid_of(ast, L"path.to.a.Lib");
    am_varid_t new_ref = varid_of(ast, L"path.to.a.Lib.foo");

    am_value_t new_alias_type = am_list_get(ast->alloc, ast->var_type, (size_t)new_alias);
    am_value_t new_ref_type = am_list_get(ast->alloc, ast->var_type, (size_t)new_ref);
    assert(am_value_is_uint(new_alias_type) && am_value_to_uint(new_alias_type) == AM_VAR_TYPE_IMPORT_ALIAS);
    assert(am_value_is_uint(new_ref_type) && am_value_to_uint(new_ref_type) == AM_VAR_TYPE_IMPORT_REF);

    // 旧别名的依赖映射已被删除，新别名的依赖映射存在且指向同一 path handle
    am_value_t old_dep = am_map_get(ast->alloc, ast->dependencies, am_make_value_of_varid(old_alias));
    am_value_t new_dep = am_map_get(ast->alloc, ast->dependencies, am_make_value_of_varid(new_alias));
    assert(!am_value_is_handle(old_dep));
    assert(am_value_is_handle(new_dep));

    // import 节点的别名已被替换为新别名
    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    am_list_t *lambda = handle_to_list(ast, top_lambda);
    assert(am_list_lambda_get_body_number(ast->alloc, lambda) == 2);

    am_value_t import_body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_handle(import_body));
    am_list_t *import_app = handle_to_list(ast, am_value_to_handle(import_body));
    assert(import_app->type == AM_LIST_TYPE_APPLICATION);
    assert(import_app->length == 3);
    am_value_t alias_child = am_list_get(ast->alloc, import_app, 1);
    assert(am_value_is_varid(alias_child));
    assert(am_value_to_varid(alias_child) == new_alias);

    // (Lib.foo x) 应用中 Lib.foo 已被替换
    am_value_t call_body = am_list_get(ast->alloc, lambda, 3);
    assert(am_value_is_handle(call_body));
    am_list_t *call_app = handle_to_list(ast, am_value_to_handle(call_body));
    assert(call_app->type == AM_LIST_TYPE_APPLICATION);
    assert(call_app->length == 2);
    am_value_t op = am_list_get(ast->alloc, call_app, 0);
    assert(am_value_is_varid(op));
    assert(am_value_to_varid(op) == new_ref);

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_parse_top_lambda_and_var_top(void) {
    printf("test_parse_top_lambda_and_var_top ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda () (define x 1) (define y 2) (define f (lambda (a) a)) (display x)))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    // top_lambda_handle 已设置
    assert(ast->top_lambda_handle != AM_HANDLE_NULL);
    assert(ast->top_lambda_handle == am_ast_get_top_lambda_node_handle(ast));

    // var_top 包含顶层 define 的变量：x, y, f
    assert(ast->var_top != NULL);
    assert(ast->var_top->length == 3);

    am_varid_t x_varid = arnid_of(ast, ast->top_lambda_handle, L"x");
    am_varid_t y_varid = arnid_of(ast, ast->top_lambda_handle, L"y");
    am_varid_t f_varid = arnid_of(ast, ast->top_lambda_handle, L"f");

    am_value_t v0 = am_list_get(ast->alloc, ast->var_top, 0);
    am_value_t v1 = am_list_get(ast->alloc, ast->var_top, 1);
    am_value_t v2 = am_list_get(ast->alloc, ast->var_top, 2);
    assert(am_value_is_varid(v0) && am_value_to_varid(v0) == x_varid);
    assert(am_value_is_varid(v1) && am_value_to_varid(v1) == y_varid);
    assert(am_value_is_varid(v2) && am_value_to_varid(v2) == f_varid);

    am_ast_destroy(ast);
    printf("OK\n");
}


static int handle_in_tailcall_list(am_ast_t *ast, am_handle_t handle) {
    if (!ast || !ast->tailcall_handles) return 0;
    for (size_t i = 0; i < ast->tailcall_handles->length; i++) {
        am_value_t v = am_list_get(ast->alloc, ast->tailcall_handles, i);
        if (am_value_is_handle(v) && am_value_to_handle(v) == handle) {
            return 1;
        }
    }
    return 0;
}


static void test_parse_tail_call_analysis(void) {
    printf("test_parse_tail_call_analysis ... ");
    test_allocator_reset();

    // 顶层 lambda 最后一个 body 是 (begin ...)，其最后一个子表达式 (display 2) 应为尾调用。
    // 顶层 application 自身也作为入口点被标记为尾调用。
    wchar_t *code = L"((lambda () (begin (display 1) (display 2))))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/test.scm");
    assert(ast != NULL);

    am_handle_t top_app = am_ast_get_top_node_handle(ast);
    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    assert(top_app != AM_HANDLE_NULL);
    assert(top_lambda != AM_HANDLE_NULL);

    am_list_t *lambda = handle_to_list(ast, top_lambda);
    size_t n_body = am_list_lambda_get_body_number(ast->alloc, lambda);
    assert(n_body == 1);

    am_value_t begin_body = am_list_get(ast->alloc, lambda, 2);
    assert(am_value_is_handle(begin_body));
    am_handle_t begin_handle = am_value_to_handle(begin_body);
    am_list_t *begin_lst = handle_to_list(ast, begin_handle);
    assert(begin_lst->type == AM_LIST_TYPE_APPLICATION);
    assert(begin_lst->length == 3);

    am_value_t display1_val = am_list_get(ast->alloc, begin_lst, 1);
    am_value_t display2_val = am_list_get(ast->alloc, begin_lst, 2);
    assert(am_value_is_handle(display1_val));
    assert(am_value_is_handle(display2_val));
    am_handle_t display1 = am_value_to_handle(display1_val);
    am_handle_t display2 = am_value_to_handle(display2_val);

    // 尾调用应为：顶层 app、begin 节点、(display 2)
    assert(ast->tailcall_handles->length == 3);
    assert(handle_in_tailcall_list(ast, top_app));
    assert(handle_in_tailcall_list(ast, begin_handle));
    assert(handle_in_tailcall_list(ast, display2));
    assert(!handle_in_tailcall_list(ast, display1));
    assert(!handle_in_tailcall_list(ast, top_lambda));

    am_ast_destroy(ast);
    printf("OK\n");
}


static void test_print_tokens(void) {
    printf("test_print_tokens ... \n");
    test_allocator_reset();

    wchar_t *code = L"((lambda () (display \"hello\") 42))";

    int32_t code_length = (int32_t)wcslen(code);
    am_token_t *tokens = (am_token_t *)calloc(code_length, sizeof(am_token_t));
    assert(tokens != NULL);

    int32_t count = am_lexer(code, tokens);
    assert(count >= 0);

    print_tokens(code, tokens, (size_t)count);

    free(tokens);
    printf("OK\n");
}


static void test_ast_print(void) {
    printf("test_ast_print ... ");
    test_allocator_reset();

    wchar_t *code = L"((lambda ()     (native Math) (import Lib \"/root/lib.scm\") (define f (lambda (x y) (define f (lambda (x) (+ x Math.PI))) (Lib.foo (f x) y))) (define x 2) (define y 3) (display (f x y))         ))";
    am_ast_t *ast = am_parser(&test_allocator, code, L"/home/bd4sur/animac/demo.scm");
    assert(ast != NULL);

    print_tokens(code, ast->tokens, (size_t)(ast->token_count));

    FILE *out = tmpfile();
    assert(out != NULL);

    ast_print(out, ast);
    rewind(out);

    size_t cap = 4096;
    size_t len = 0;
    wchar_t *buf = (wchar_t *)malloc(cap * sizeof(wchar_t));
    assert(buf != NULL);

    wint_t c;
    while ((c = fgetwc(out)) != WEOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (wchar_t *)realloc(buf, cap * sizeof(wchar_t));
            assert(buf != NULL);
        }
        buf[len++] = (wchar_t)c;
    }
    buf[len] = L'\0';

    // assert(wcsstr(buf, L"AST {") != NULL);
    // assert(wcsstr(buf, L"absolute_path: \"/demo.scm\"") != NULL);
    // assert(wcsstr(buf, L"module_id: \".demo\"") != NULL);
    // assert(wcsstr(buf, L"LAMBDA") != NULL);
    // assert(wcsstr(buf, L"APPLICATION") != NULL);
    // assert(wcsstr(buf, L"\"x\"") != NULL);
    // assert(wcsstr(buf, L"\"y\"") != NULL);
    // assert(wcsstr(buf, L"\"display\"") != NULL);
    // assert(wcsstr(buf, L"42") != NULL);
    // assert(wcsstr(buf, L"parameters:") != NULL);
    // assert(wcsstr(buf, L"bodies:") != NULL);
    // assert(wcsstr(buf, L"children:") != NULL);
    assert(wcsstr(buf, L"nodes:") != NULL);
    // assert(wcsstr(buf, L"WSTRING") != NULL);

    fclose(out);

    // 同时将可视化结果输出到 stdout，便于直观查看
    printf("\n%ls\n", buf);

    free(buf);
    am_ast_destroy(ast);
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

    test_parse_empty_arglist();
    test_parse_number_literal();
    test_parse_negative_number();
    test_parse_string_literal();
    test_parse_application();
    test_parse_lambda();
    test_parse_quote_shorthand();
    test_parse_quote_list();
    test_parse_quote_keyword();
    test_parse_unquote_in_quasiquote();
    test_parse_import_native();
    test_parse_alpha_renaming();
    test_parse_alpha_renaming_nested();
    test_parse_import_alias_rename();
    test_parse_top_lambda_and_var_top();
    test_parse_tail_call_analysis();
    test_print_tokens();
    test_ast_print();

    test_destroy(&test_allocator_state);

    printf("\nAll parser tests passed.\n");
    return 0;
}
