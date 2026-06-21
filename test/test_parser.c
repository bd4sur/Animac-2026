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
#include "highlight.h"
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
// 相关实现已提取到 src/debug.c 与 include/debug.h，这里直接调用：
//   am_debug_ast_print(FILE *out, am_ast_t *ast)
//   am_debug_ast_print_to_stdout(am_ast_t *ast)
//   am_debug_ast_print_node_summary(FILE *out, am_ast_t *ast, am_handle_t handle)

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


static void test_ast_print(wchar_t *path) {
    printf("test_ast_print ... \n");
    test_allocator_reset();

    wchar_t *file_content = read_file_as_wstring(path);
    assert(file_content != NULL);

    const wchar_t *prefix = L"((lambda () ";
    const wchar_t *suffix = L" ))";
    size_t prefix_len = wcslen(prefix);
    size_t suffix_len = wcslen(suffix);
    size_t content_len = wcslen(file_content);
    size_t code_len = prefix_len + content_len + suffix_len;

    wchar_t *code = (wchar_t *)malloc((code_len + 1) * sizeof(wchar_t));
    assert(code != NULL);
    size_t pos = 0;
    for (size_t i = 0; i < prefix_len; i++) code[pos++] = prefix[i];
    for (size_t i = 0; i < content_len; i++) code[pos++] = file_content[i];
    for (size_t i = 0; i < suffix_len; i++) code[pos++] = suffix[i];
    code[pos] = L'\0';
    free(file_content);

    am_ast_t *ast = am_parser(&test_allocator, code, path);
    assert(ast != NULL);

    printf("=== highlighted code ===\n");
    am_print_highlighted(code, ast->tokens, (int32_t)(ast->token_count));
    printf("\n=== tokens ===\n");

    print_tokens(code, ast->tokens, (size_t)(ast->token_count));

    free(code);

    FILE *out = tmpfile();
    assert(out != NULL);

    am_debug_ast_print(out, ast);
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
// AST merge 测试辅助函数
// ===============================================================================

static am_ast_t *parse_file(const wchar_t *path) {
    wchar_t *file_content = read_file_as_wstring(path);
    assert(file_content != NULL);

    const wchar_t *prefix = L"((lambda () ";
    const wchar_t *suffix = L" ))";
    size_t code_len = wcslen(prefix) + wcslen(file_content) + wcslen(suffix);

    size_t prefix_len2 = wcslen(prefix);
    size_t suffix_len2 = wcslen(suffix);
    size_t content_len2 = wcslen(file_content);
    wchar_t *code = (wchar_t *)am_malloc(&test_allocator, (code_len + 1) * sizeof(wchar_t));
    assert(code != NULL);
    size_t pos2 = 0;
    for (size_t i = 0; i < prefix_len2; i++) code[pos2++] = prefix[i];
    for (size_t i = 0; i < content_len2; i++) code[pos2++] = file_content[i];
    for (size_t i = 0; i < suffix_len2; i++) code[pos2++] = suffix[i];
    code[pos2] = L'\0';
    free(file_content);

    am_ast_t *ast = am_parser(&test_allocator, code, (wchar_t *)path);
    assert(ast != NULL);
    return ast;
}


static void assert_unique_var_exists(am_ast_t *ast, const wchar_t *module_id,
                                      am_handle_t lambda_handle, const wchar_t *name) {
    wchar_t buf[256];
    int n = swprintf(buf, 256, L"%ls.%zu.%ls", module_id, lambda_handle, name);
    assert(n > 0 && n < 256);
    assert(am_vocab_find(ast->alloc, ast->var_vocab, buf) != SIZE_MAX);
}


static am_varid_t unique_varid_of(am_ast_t *ast, const wchar_t *module_id,
                                   am_handle_t lambda_handle, const wchar_t *name) {
    wchar_t buf[256];
    int n = swprintf(buf, 256, L"%ls.%zu.%ls", module_id, lambda_handle, name);
    assert(n > 0 && n < 256);
    size_t idx = am_vocab_find(ast->alloc, ast->var_vocab, buf);
    assert(idx != SIZE_MAX);
    return (am_varid_t)idx;
}


static int var_top_contains_varid(am_ast_t *ast, am_varid_t varid) {
    for (size_t i = 0; i < ast->var_top->length; i++) {
        am_value_t v = am_list_get(ast->alloc, ast->var_top, i);
        if (am_value_is_varid(v) && am_value_to_varid(v) == varid) return 1;
    }
    return 0;
}


static void test_ast_merge(const wchar_t *importer_path, const wchar_t *importee_path) {
    printf("test_ast_merge ... \n");
    test_allocator_reset();

    am_ast_t *importer = parse_file(importer_path);
    am_ast_t *importee = parse_file(importee_path);

    am_handle_t importer_top_lambda = importer->top_lambda_handle;
    am_handle_t importee_top_lambda = importee->top_lambda_handle;
    assert(importer_top_lambda != AM_HANDLE_NULL);
    assert(importee_top_lambda != AM_HANDLE_NULL);

    // merge 前分别输出 importer 与 importee 的 AST
    printf("=== importer AST before merge ===\n");
    am_debug_ast_print_to_stdout(importer);
    printf("=== importee AST before merge ===\n");
    am_debug_ast_print_to_stdout(importee);

    int32_t merge_result = am_ast_merge(importer, importee, 0);
    assert(merge_result == 0);

    // module_id 保持不变
    assert(wcscmp(importer->module_id, L"home.bd4sur.animac.x") == 0);

    // 验证 symbol / var 词汇表已合并
    assert_unique_var_exists(importer, L"home.bd4sur.animac.y", importee_top_lambda, L"f");
    assert_unique_var_exists(importer, L"home.bd4sur.animac.y", importee_top_lambda, L"x");
    assert_unique_var_exists(importer, L"home.bd4sur.animac.y", importee_top_lambda, L"y");
    assert_unique_var_exists(importer, L"home.bd4sur.animac.y", importee_top_lambda, L"z");
    assert_unique_var_exists(importer, L"home.bd4sur.animac.x", importer_top_lambda, L"f");
    assert_unique_var_exists(importer, L"home.bd4sur.animac.x", importer_top_lambda, L"x");
    assert_unique_var_exists(importer, L"home.bd4sur.animac.x", importer_top_lambda, L"y");
    assert_unique_var_exists(importer, L"home.bd4sur.animac.x", importer_top_lambda, L"z");

    // 验证 dependencies 已合并：共 3 条
    size_t dep_count = am_map_length(importer->alloc, importer->dependencies);
    assert(dep_count == 3);

    am_varid_t y_z_alias = varid_of(importer, L"home.bd4sur.animac.y.z");
    am_varid_t x_z_alias = varid_of(importer, L"home.bd4sur.animac.x.z");
    am_varid_t x_y_alias = varid_of(importer, L"home.bd4sur.animac.x.y");

    am_value_t y_z_path = am_map_get(importer->alloc, importer->dependencies,
                                      am_make_value_of_varid(y_z_alias));
    am_value_t x_z_path = am_map_get(importer->alloc, importer->dependencies,
                                      am_make_value_of_varid(x_z_alias));
    am_value_t x_y_path = am_map_get(importer->alloc, importer->dependencies,
                                      am_make_value_of_varid(x_y_alias));
    assert(am_value_is_handle(y_z_path));
    assert(am_value_is_handle(x_z_path));
    assert(am_value_is_handle(x_y_path));

    am_wstring_t *y_z_ws = handle_to_wstring(importer, am_value_to_handle(y_z_path));
    am_wstring_t *x_z_ws = handle_to_wstring(importer, am_value_to_handle(x_z_path));
    am_wstring_t *x_y_ws = handle_to_wstring(importer, am_value_to_handle(x_y_path));
    wchar_t *y_z_str = wstring_to_buf(y_z_ws);
    wchar_t *x_z_str = wstring_to_buf(x_z_ws);
    wchar_t *x_y_str = wstring_to_buf(x_y_ws);
    assert(wcscmp(y_z_str, L"\"/home/bd4sur/animac/z.scm\"") == 0);
    assert(wcscmp(x_z_str, L"\"/home/bd4sur/animac/y.scm\"") == 0);
    assert(wcscmp(x_y_str, L"\"/home/bd4sur/animac/z.scm\"") == 0);
    free(y_z_str); free(x_z_str); free(x_y_str);

    // 验证顶层 lambda 的函数体数量：importee 5 个 + importer 6 个 = 11
    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(importer);
    am_list_t *lambda = handle_to_list(importer, top_lambda);
    size_t n_body = am_list_lambda_get_body_number(importer->alloc, lambda);
    assert(n_body == 11);

    // 验证顺序：order=0 时 importee 的顶级节点在前
    // importee 第一个节点是 (import home.bd4sur.animac.y.z "...")
    am_value_t first_body = am_list_get(importer->alloc, lambda, 2);
    assert(am_value_is_handle(first_body));
    am_list_t *first_app = handle_to_list(importer, am_value_to_handle(first_body));
    assert(first_app->type == AM_LIST_TYPE_APPLICATION);
    assert(first_app->length == 3);
    am_value_t first_sym = am_list_get(importer->alloc, first_app, 0);
    assert(am_value_is_symbol(first_sym));
    assert(am_value_to_symbol(first_sym) == symbol_of(importer, L"import"));
    am_value_t first_alias = am_list_get(importer->alloc, first_app, 1);
    assert(am_value_is_varid(first_alias));
    assert(am_value_to_varid(first_alias) == y_z_alias);
    am_value_t first_path = am_list_get(importer->alloc, first_app, 2);
    assert(am_value_is_handle(first_path));
    am_wstring_t *first_path_ws = handle_to_wstring(importer, am_value_to_handle(first_path));
    wchar_t *first_path_str = wstring_to_buf(first_path_ws);
    assert(wcscmp(first_path_str, L"\"/home/bd4sur/animac/z.scm\"") == 0);
    free(first_path_str);

    // importer 第一个节点在 importee 5 个节点之后：(import home.bd4sur.animac.x.z "...")
    am_value_t x_first_body = am_list_get(importer->alloc, lambda, 2 + 5);
    assert(am_value_is_handle(x_first_body));
    am_list_t *x_first_app = handle_to_list(importer, am_value_to_handle(x_first_body));
    assert(x_first_app->type == AM_LIST_TYPE_APPLICATION);
    assert(x_first_app->length == 3);
    am_value_t x_first_alias = am_list_get(importer->alloc, x_first_app, 1);
    assert(am_value_is_varid(x_first_alias));
    assert(am_value_to_varid(x_first_alias) == x_z_alias);
    am_value_t x_first_path = am_list_get(importer->alloc, x_first_app, 2);
    assert(am_value_is_handle(x_first_path));
    am_wstring_t *x_first_path_ws = handle_to_wstring(importer, am_value_to_handle(x_first_path));
    wchar_t *x_first_path_str = wstring_to_buf(x_first_path_ws);
    assert(wcscmp(x_first_path_str, L"\"/home/bd4sur/animac/y.scm\"") == 0);
    free(x_first_path_str);

    // 验证 var_top 已合并：共 8 个顶级变量
    assert(importer->var_top->length == 8);
    assert(var_top_contains_varid(importer, unique_varid_of(importer, L"home.bd4sur.animac.y",
                                                              importee_top_lambda, L"f")));
    assert(var_top_contains_varid(importer, unique_varid_of(importer, L"home.bd4sur.animac.y",
                                                              importee_top_lambda, L"x")));
    assert(var_top_contains_varid(importer, unique_varid_of(importer, L"home.bd4sur.animac.y",
                                                              importee_top_lambda, L"y")));
    assert(var_top_contains_varid(importer, unique_varid_of(importer, L"home.bd4sur.animac.y",
                                                              importee_top_lambda, L"z")));
    assert(var_top_contains_varid(importer, unique_varid_of(importer, L"home.bd4sur.animac.x",
                                                              importer_top_lambda, L"f")));
    assert(var_top_contains_varid(importer, unique_varid_of(importer, L"home.bd4sur.animac.x",
                                                              importer_top_lambda, L"x")));
    assert(var_top_contains_varid(importer, unique_varid_of(importer, L"home.bd4sur.animac.x",
                                                              importer_top_lambda, L"y")));
    assert(var_top_contains_varid(importer, unique_varid_of(importer, L"home.bd4sur.animac.x",
                                                              importer_top_lambda, L"z")));

    // 输出合并后的 AST 便于直观检查
    am_debug_ast_print_to_stdout(importer);

    am_ast_destroy(importee);
    am_ast_destroy(importer);
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
    test_ast_print(L"/home/bd4sur/animac/test.scm");
    test_ast_merge(L"/home/bd4sur/animac/x.scm", L"/home/bd4sur/animac/y.scm");

    test_destroy(&test_allocator_state);

    printf("\nAll parser tests passed.\n");
    return 0;
}
