#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wchar.h>
#include <locale.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "ast.h"
#include "object.h"
#include "list.h"
#include "wstring.h"
#pragma GCC diagnostic pop

// ===============================================================================
// 基础设施：基于内存池的简单测试分配器（bump allocator）
// ===============================================================================

#define TEST_POOL_SIZE (1024 * 1024)

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
    // bump allocator: no-op
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
// 测试用例
// ===============================================================================

static void test_ast_create_destroy(void) {
    printf("test_ast_create_destroy ... ");
    test_allocator_reset();

    wchar_t code[] = L"((lambda () 42))";
    wchar_t path[] = L"/home/user/test.scm";
    am_token_t tokens[64];
    int32_t count = am_lexer(code, tokens);
    assert(count > 0);

    am_ast_t *ast = am_ast_create(&test_allocator, code, path, tokens, (size_t)count);
    assert(ast != NULL);
    assert(ast->symbol_vocab != NULL);
    assert(ast->var_vocab != NULL);
    assert(ast->nodes != NULL);
    assert(wcscmp(ast->module_id, L"home.user.test") == 0);

    assert(am_ast_destroy(ast) == 0);
    printf("OK\n");
}


static void test_build_vocabularies(void) {
    printf("test_build_vocabularies ... ");
    test_allocator_reset();

    wchar_t code[] = L"(define foo (lambda (x y) (+ x y)))";
    wchar_t path[] = L"/tmp/test.scm";
    am_token_t tokens[64];
    int32_t count = am_lexer(code, tokens);
    assert(count > 0);

    am_ast_t *ast = am_ast_create(&test_allocator, code, path, tokens, (size_t)count);
    assert(ast != NULL);

    size_t sym_count = am_build_symbol_vocabulary(ast);
    assert(sym_count >= AM_KEYWORDS_NUM);

    // lambda, define, + 应该是 keyword
    for (int32_t i = 0; i < count; i++) {
        if (tokens[i].type == AM_TOKEN_TYPE_KEYWORD) {
            assert(tokens[i].id < AM_KEYWORDS_NUM);
        }
    }

    size_t var_count = am_build_variable_vocabulary(ast);
    assert(var_count > 0);

    // foo, x, y 应该是 variable
    int found_foo = 0, found_x = 0, found_y = 0;
    for (int32_t i = 0; i < count; i++) {
        if (tokens[i].type == AM_TOKEN_TYPE_IDENTIFIER) {
            wchar_t *text = (wchar_t *)malloc((tokens[i].length + 1) * sizeof(wchar_t));
            wcsncpy(text, &code[tokens[i].index], tokens[i].length);
            text[tokens[i].length] = L'\0';
            if (wcscmp(text, L"foo") == 0) found_foo = 1;
            if (wcscmp(text, L"x") == 0) found_x = 1;
            if (wcscmp(text, L"y") == 0) found_y = 1;
            free(text);
        }
    }
    assert(found_foo && found_x && found_y);

    assert(am_ast_destroy(ast) == 0);
    printf("OK\n");
}


static void test_make_nodes(void) {
    printf("test_make_nodes ... ");
    test_allocator_reset();

    wchar_t code[] = L"((lambda () 42))";
    wchar_t path[] = L"/test.scm";
    am_token_t tokens[64];
    int32_t count = am_lexer(code, tokens);
    assert(count > 0);

    am_ast_t *ast = am_ast_create(&test_allocator, code, path, tokens, (size_t)count);
    assert(ast != NULL);

    // 创建顶层 application: ((lambda () 42))
    am_handle_t app = am_ast_make_slist_node(ast, AM_TOP_NODE_HANDLE, AM_LIST_TYPE_APPLICATION);
    assert(app != AM_HANDLE_NULL);

    // 创建顶层 lambda
    am_handle_t lambda = am_ast_make_lambda_node(ast, app);
    assert(lambda != AM_HANDLE_NULL);

    // 把 lambda 加到 app 中
    am_value_t app_val = am_ast_get_node(ast, app);
    am_list_t *app_lst = (am_list_t *)am_value_to_ptr(app_val);
    am_list_t *new_app = am_list_push(&test_allocator, app_lst, am_make_value_of_handle(lambda));
    assert(new_app != NULL);
    if (new_app != app_lst) {
        assert(am_heap_set(&test_allocator, &test_allocator, ast->nodes, app, am_make_value_of_ptr((am_object_t *)new_app)) == 0);
    }

    // 添加一个 body: 42 (uint)
    am_value_t lambda_val = am_ast_get_node(ast, lambda);
    am_list_t *lambda_lst = (am_list_t *)am_value_to_ptr(lambda_val);
    am_list_t *new_lambda = am_list_lambda_add_body(&test_allocator, lambda_lst, am_make_value_of_uint(42));
    assert(new_lambda != NULL);
    if (new_lambda != lambda_lst) {
        assert(am_heap_set(&test_allocator, &test_allocator, ast->nodes, lambda, am_make_value_of_ptr((am_object_t *)new_lambda)) == 0);
    }

    // 查找顶级节点
    am_handle_t top_app = am_ast_get_top_node_handle(ast);
    assert(top_app == app);

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    assert(top_lambda == lambda);

    // 获取全局 nodes
    am_value_t *bodies = am_ast_get_global_nodes(ast);
    assert(bodies != NULL);
    assert(am_value_is_uint(bodies[0]));
    assert(am_value_to_uint(bodies[0]) == 42);
    free(bodies);

    assert(am_ast_destroy(ast) == 0);
    printf("OK\n");
}


static void test_make_wstring_node(void) {
    printf("test_make_wstring_node ... ");
    test_allocator_reset();

    wchar_t code[] = L"\"hello world\"";
    wchar_t path[] = L"/test.scm";
    am_token_t tokens[64];
    int32_t count = am_lexer(code, tokens);
    assert(count == 1);
    assert(tokens[0].type == AM_TOKEN_TYPE_STRING);

    am_ast_t *ast = am_ast_create(&test_allocator, code, path, tokens, (size_t)count);
    assert(ast != NULL);

    am_handle_t str_handle = am_ast_make_wstring_node(ast, &tokens[0]);
    assert(str_handle != AM_HANDLE_NULL);

    am_value_t v = am_ast_get_node(ast, str_handle);
    assert(am_value_is_ptr(v));
    am_wstring_t *ws = (am_wstring_t *)am_value_to_ptr(v);
    assert(ws->base.type == AM_OBJECT_TYPE_WSTRING);
    assert(ws->length == tokens[0].length);

    assert(am_ast_destroy(ast) == 0);
    printf("OK\n");
}


static void test_unique_variable(void) {
    printf("test_unique_variable ... ");
    test_allocator_reset();

    wchar_t code[] = L"x";
    wchar_t path[] = L"/test.scm";
    am_token_t tokens[64];
    int32_t count = am_lexer(code, tokens);
    assert(count == 1);

    am_ast_t *ast = am_ast_create(&test_allocator, code, path, tokens, (size_t)count);
    assert(ast != NULL);

    am_build_variable_vocabulary(ast);
    am_varid_t x_varid = tokens[0].id;

    am_varid_t unique = am_ast_make_unique_variable(ast, x_varid, 7);
    assert(unique != SIZE_MAX);
    assert(unique != x_varid);

    wchar_t *unique_str = am_vocab_get(&test_allocator, ast->var_vocab, &unique);
    assert(unique_str != NULL);
    // assert(wcsstr(unique_str, L"V.") == unique_str);
    assert(wcsstr(unique_str, L".7.") != NULL);

    assert(am_ast_destroy(ast) == 0);
    printf("OK\n");
}


static void test_find_nearest_lambda(void) {
    printf("test_find_nearest_lambda ... ");
    test_allocator_reset();

    wchar_t code[] = L"((lambda () 42))";
    wchar_t path[] = L"/test.scm";
    am_token_t tokens[64];
    int32_t count = am_lexer(code, tokens);
    assert(count > 0);

    am_ast_t *ast = am_ast_create(&test_allocator, code, path, tokens, (size_t)count);
    assert(ast != NULL);

    am_handle_t app = am_ast_make_slist_node(ast, AM_TOP_NODE_HANDLE, AM_LIST_TYPE_APPLICATION);
    am_handle_t lambda = am_ast_make_lambda_node(ast, app);

    am_value_t app_val = am_ast_get_node(ast, app);
    am_list_t *app_lst = (am_list_t *)am_value_to_ptr(app_val);
    am_list_t *new_app = am_list_push(&test_allocator, app_lst, am_make_value_of_handle(lambda));
    if (new_app != app_lst) {
        am_heap_set(&test_allocator, &test_allocator, ast->nodes, app, am_make_value_of_ptr((am_object_t *)new_app));
    }

    am_handle_t found = am_ast_find_nearest_lambda_handle(ast, lambda);
    assert(found == lambda);

    assert(am_ast_destroy(ast) == 0);
    printf("OK\n");
}


static void test_copy(void) {
    printf("test_copy ... ");
    test_allocator_reset();

    wchar_t code[] = L"((lambda () 42))";
    wchar_t path[] = L"/test.scm";
    am_token_t tokens[64];
    int32_t count = am_lexer(code, tokens);
    assert(count > 0);

    am_ast_t *ast = am_ast_create(&test_allocator, code, path, tokens, (size_t)count);
    assert(ast != NULL);

    am_build_symbol_vocabulary(ast);
    am_build_variable_vocabulary(ast);

    am_handle_t app = am_ast_make_slist_node(ast, AM_TOP_NODE_HANDLE, AM_LIST_TYPE_APPLICATION);
    am_handle_t lambda = am_ast_make_lambda_node(ast, app);

    am_value_t app_val = am_ast_get_node(ast, app);
    am_list_t *app_lst = (am_list_t *)am_value_to_ptr(app_val);
    am_list_t *new_app = am_list_push(&test_allocator, app_lst, am_make_value_of_handle(lambda));
    if (new_app != app_lst) {
        am_heap_set(&test_allocator, &test_allocator, ast->nodes, app, am_make_value_of_ptr((am_object_t *)new_app));
    }

    am_value_t lambda_val = am_ast_get_node(ast, lambda);
    am_list_t *lambda_lst = (am_list_t *)am_value_to_ptr(lambda_val);
    am_list_t *new_lambda = am_list_lambda_add_body(&test_allocator, lambda_lst, am_make_value_of_uint(42));
    if (new_lambda != lambda_lst) {
        am_heap_set(&test_allocator, &test_allocator, ast->nodes, lambda, am_make_value_of_ptr((am_object_t *)new_lambda));
    }

    am_ast_t *copy = am_ast_copy(ast);
    assert(copy != NULL);
    assert(copy->symbol_vocab->length == ast->symbol_vocab->length);
    assert(copy->var_vocab->length == ast->var_vocab->length);

    am_handle_t copy_top_app = am_ast_get_top_node_handle(copy);
    assert(copy_top_app == app);
    am_handle_t copy_top_lambda = am_ast_get_top_lambda_node_handle(copy);
    assert(copy_top_lambda == lambda);

    am_value_t *bodies = am_ast_get_global_nodes(copy);
    assert(bodies != NULL);
    assert(am_value_to_uint(bodies[0]) == 42);
    free(bodies);

    assert(am_ast_destroy(copy) == 0);
    assert(am_ast_destroy(ast) == 0);
    printf("OK\n");
}


static void test_node_to_string(void) {
    printf("test_node_to_string ... ");
    test_allocator_reset();

    wchar_t code[] = L"\"hello world\" ((lambda (x y) (+ x y) 42))";
    wchar_t path[] = L"/test.scm";
    am_token_t tokens[64];
    int32_t count = am_lexer(code, tokens);
    assert(count > 0);

    am_ast_t *ast = am_ast_create(&test_allocator, code, path, tokens, (size_t)count);
    assert(ast != NULL);

    am_build_symbol_vocabulary(ast);
    am_build_variable_vocabulary(ast);

    // 创建顶层 application: ((lambda (x y) (+ x y) 42))
    am_handle_t app = am_ast_make_slist_node(ast, AM_TOP_NODE_HANDLE, AM_LIST_TYPE_APPLICATION);
    assert(app != AM_HANDLE_NULL);

    am_handle_t lambda = am_ast_make_lambda_node(ast, app);
    assert(lambda != AM_HANDLE_NULL);

    // app -> lambda
    {
        am_value_t v = am_ast_get_node(ast, app);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_push(&test_allocator, lst, am_make_value_of_handle(lambda));
        assert(n != NULL);
        if (n != lst) {
            assert(am_heap_set(&test_allocator, &test_allocator, ast->nodes, app, am_make_value_of_ptr((am_object_t *)n)) == 0);
        }
    }

    // lambda 参数 x, y
    am_varid_t x_varid = am_vocab_find(&test_allocator, ast->var_vocab, L"x");
    am_varid_t y_varid = am_vocab_find(&test_allocator, ast->var_vocab, L"y");
    assert(x_varid != SIZE_MAX && y_varid != SIZE_MAX);
    {
        am_value_t v = am_ast_get_node(ast, lambda);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_lambda_add_parameter(&test_allocator, lst, am_make_value_of_varid(x_varid));
        assert(n != NULL);
        if (n != lst) {
            assert(am_heap_set(&test_allocator, &test_allocator, ast->nodes, lambda, am_make_value_of_ptr((am_object_t *)n)) == 0);
        }
    }
    {
        am_value_t v = am_ast_get_node(ast, lambda);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_lambda_add_parameter(&test_allocator, lst, am_make_value_of_varid(y_varid));
        assert(n != NULL);
        if (n != lst) {
            assert(am_heap_set(&test_allocator, &test_allocator, ast->nodes, lambda, am_make_value_of_ptr((am_object_t *)n)) == 0);
        }
    }

    // lambda body1: (+ x y)
    am_handle_t body1 = am_ast_make_slist_node(ast, lambda, AM_LIST_TYPE_APPLICATION);
    assert(body1 != AM_HANDLE_NULL);
    {
        am_varid_t plus_varid = am_vocab_find(&test_allocator, ast->var_vocab, L"+");
        assert(plus_varid != SIZE_MAX);

        am_value_t v = am_ast_get_node(ast, body1);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_push(&test_allocator, lst, am_make_value_of_varid(plus_varid));
        assert(n != NULL);
        if (n != lst) {
            assert(am_heap_set(&test_allocator, &test_allocator, ast->nodes, body1, am_make_value_of_ptr((am_object_t *)n)) == 0);
        }
    }
    {
        am_value_t v = am_ast_get_node(ast, body1);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_push(&test_allocator, lst, am_make_value_of_varid(x_varid));
        assert(n != NULL);
        if (n != lst) {
            assert(am_heap_set(&test_allocator, &test_allocator, ast->nodes, body1, am_make_value_of_ptr((am_object_t *)n)) == 0);
        }
    }
    {
        am_value_t v = am_ast_get_node(ast, body1);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_push(&test_allocator, lst, am_make_value_of_varid(y_varid));
        assert(n != NULL);
        if (n != lst) {
            assert(am_heap_set(&test_allocator, &test_allocator, ast->nodes, body1, am_make_value_of_ptr((am_object_t *)n)) == 0);
        }
    }

    // 将 body1 加入 lambda
    {
        am_value_t v = am_ast_get_node(ast, lambda);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_lambda_add_body(&test_allocator, lst, am_make_value_of_handle(body1));
        assert(n != NULL);
        if (n != lst) {
            assert(am_heap_set(&test_allocator, &test_allocator, ast->nodes, lambda, am_make_value_of_ptr((am_object_t *)n)) == 0);
        }
    }
    // lambda body2: 42
    {
        am_value_t v = am_ast_get_node(ast, lambda);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_lambda_add_body(&test_allocator, lst, am_make_value_of_uint(42));
        assert(n != NULL);
        if (n != lst) {
            assert(am_heap_set(&test_allocator, &test_allocator, ast->nodes, lambda, am_make_value_of_ptr((am_object_t *)n)) == 0);
        }
    }

    // 测试顶层 application 输出
    size_t len = 0;
    wchar_t *s = am_ast_node_to_string(&test_allocator, ast, app, &len);
    assert(s != NULL);
    assert(wcscmp(s, L"((lambda (x y) (+ x y) 42))") == 0);
    assert(len == wcslen(s));

    // 测试 lambda 节点输出
    s = am_ast_node_to_string(&test_allocator, ast, lambda, &len);
    assert(s != NULL);
    assert(wcscmp(s, L"(lambda (x y) (+ x y) 42)") == 0);

    // 测试 application 节点输出
    s = am_ast_node_to_string(&test_allocator, ast, body1, &len);
    assert(s != NULL);
    assert(wcscmp(s, L"(+ x y)") == 0);

    // 测试 quote 列表: '(x y)
    am_handle_t quote = am_ast_make_slist_node(ast, lambda, AM_LIST_TYPE_QUOTE);
    assert(quote != AM_HANDLE_NULL);
    {
        am_value_t v = am_ast_get_node(ast, quote);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_push(&test_allocator, lst, am_make_value_of_varid(x_varid));
        assert(n != NULL);
        if (n != lst) am_heap_set(&test_allocator, &test_allocator, ast->nodes, quote, am_make_value_of_ptr((am_object_t *)n));
    }
    {
        am_value_t v = am_ast_get_node(ast, quote);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_push(&test_allocator, lst, am_make_value_of_varid(y_varid));
        assert(n != NULL);
        if (n != lst) am_heap_set(&test_allocator, &test_allocator, ast->nodes, quote, am_make_value_of_ptr((am_object_t *)n));
    }
    s = am_ast_node_to_string(&test_allocator, ast, quote, &len);
    assert(s != NULL);
    assert(wcscmp(s, L"'(x y)") == 0);

    // 测试空 quote 列表: '()
    am_handle_t empty_quote = am_ast_make_slist_node(ast, lambda, AM_LIST_TYPE_QUOTE);
    assert(empty_quote != AM_HANDLE_NULL);
    s = am_ast_node_to_string(&test_allocator, ast, empty_quote, &len);
    assert(s != NULL);
    assert(wcscmp(s, L"'()") == 0);

    // 测试字符串节点
    am_token_t str_tok;
    str_tok.index = 0;
    str_tok.length = 13; // "hello world"
    am_handle_t str_handle = am_ast_make_wstring_node(ast, &str_tok);
    assert(str_handle != AM_HANDLE_NULL);
    s = am_ast_node_to_string(&test_allocator, ast, str_handle, &len);
    assert(s != NULL);
    assert(wcscmp(s, L"\"hello world\"") == 0);

    assert(am_ast_destroy(ast) == 0);
    printf("OK\n");
}


static void test_merge(void) {
    printf("test_merge ... ");
    test_allocator_reset();

    // 目标 AST: ((lambda () 100))
    wchar_t code1[] = L"((lambda () 100))";
    wchar_t path1[] = L"/main.scm";
    am_token_t tokens1[64];
    int32_t count1 = am_lexer(code1, tokens1);
    assert(count1 > 0);

    am_ast_t *target = am_ast_create(&test_allocator, code1, path1, tokens1, (size_t)count1);
    assert(target != NULL);

    am_handle_t app1 = am_ast_make_slist_node(target, AM_TOP_NODE_HANDLE, AM_LIST_TYPE_APPLICATION);
    am_handle_t lambda1 = am_ast_make_lambda_node(target, app1);
    {
        am_value_t v = am_ast_get_node(target, app1);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_push(&test_allocator, lst, am_make_value_of_handle(lambda1));
        if (n != lst) am_heap_set(&test_allocator, &test_allocator, target->nodes, app1, am_make_value_of_ptr((am_object_t *)n));
    }
    {
        am_value_t v = am_ast_get_node(target, lambda1);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_lambda_add_body(&test_allocator, lst, am_make_value_of_uint(100));
        if (n != lst) am_heap_set(&test_allocator, &test_allocator, target->nodes, lambda1, am_make_value_of_ptr((am_object_t *)n));
    }

    // 源 AST: ((lambda () 200))
    wchar_t code2[] = L"((lambda () 200))";
    wchar_t path2[] = L"/lib.scm";
    am_token_t tokens2[64];
    int32_t count2 = am_lexer(code2, tokens2);
    assert(count2 > 0);

    am_ast_t *source = am_ast_create(&test_allocator, code2, path2, tokens2, (size_t)count2);
    assert(source != NULL);

    am_handle_t app2 = am_ast_make_slist_node(source, AM_TOP_NODE_HANDLE, AM_LIST_TYPE_APPLICATION);
    am_handle_t lambda2 = am_ast_make_lambda_node(source, app2);
    {
        am_value_t v = am_ast_get_node(source, app2);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_push(&test_allocator, lst, am_make_value_of_handle(lambda2));
        if (n != lst) am_heap_set(&test_allocator, &test_allocator, source->nodes, app2, am_make_value_of_ptr((am_object_t *)n));
    }
    {
        am_value_t v = am_ast_get_node(source, lambda2);
        am_list_t *lst = (am_list_t *)am_value_to_ptr(v);
        am_list_t *n = am_list_lambda_add_body(&test_allocator, lst, am_make_value_of_uint(200));
        if (n != lst) am_heap_set(&test_allocator, &test_allocator, source->nodes, lambda2, am_make_value_of_ptr((am_object_t *)n));
    }

    // 设置顶层 lambda 把柄，便于 am_ast_merge 和后续检查定位
    target->top_lambda_handle = lambda1;
    source->top_lambda_handle = lambda2;

    // 合并：源前置（order=0 表示 importee 的顶级节点置于 importer 之前）
    assert(am_ast_merge(target, source, 0) == 0);

    am_value_t *bodies = am_ast_get_global_nodes(target);
    assert(bodies != NULL);
    assert(am_value_to_uint(bodies[0]) == 200);
    assert(am_value_to_uint(bodies[1]) == 100);
    free(bodies);

    assert(am_ast_destroy(source) == 0);
    assert(am_ast_destroy(target) == 0);
    printf("OK\n");
}


// ===============================================================================
// 入口
// ===============================================================================

int main(void) {
    if (!setlocale(LC_ALL, "")) {
        fprintf(stderr, "setlocale failed\n");
        return 1;
    }

    printf("Running AST tests...\n");
    test_allocator_init();

    test_ast_create_destroy();
    test_build_vocabularies();
    test_make_nodes();
    test_make_wstring_node();
    test_unique_variable();
    test_find_nearest_lambda();
    test_copy();
    test_node_to_string();
    test_merge();

    test_destroy(test_allocator.state);
    printf("All AST tests passed.\n");
    return 0;
}
