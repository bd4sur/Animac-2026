#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>

#include "object.h"
#include "allocator.h"
#include "lexer.h"
#include "vocab.h"
#include "heap.h"
#include "map.h"
#include "list.h"
#include "scope.h"
#include "wstring.h"
#include "ast.h"


// ===============================================================================
// 内部辅助函数
// ===============================================================================

// 将模块绝对路径转换为模块ID。
// 规则（对应TS的PathUtils.PathToModuleID）：将斜杠/反斜杠替换为点号，空格替换为下划线，去掉冒号，去掉.scm后缀。
// 返回新分配的 wchar_t*（使用ast分配器），失败返回NULL。
static wchar_t *path_to_module_id(am_allocator_t *alloc, const wchar_t *absolute_path) {
    if (!absolute_path) return NULL;

    size_t len = wcslen(absolute_path);
    wchar_t *module_id = (wchar_t *)am_malloc(alloc, (len + 1) * sizeof(wchar_t));
    if (!module_id) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        wchar_t c = absolute_path[i];
        if (i == 0 && c == L'/') {
            // 首字符为/时直接去掉
        }
        else if (c == L'/' || c == L'\\') {
            module_id[j++] = L'.';
        }
        else if (c == L' ') {
            module_id[j++] = L'_';
        }
        else if (c == L':') {
            // 去掉冒号
        }
        else {
            module_id[j++] = c;
        }
    }
    module_id[j] = L'\0';

    // 去掉末尾的 .scm（不区分大小写）
    size_t id_len = wcslen(module_id);
    if (id_len >= 4) {
        if (module_id[id_len - 4] == L'.' &&
            (module_id[id_len - 3] == L's' || module_id[id_len - 3] == L'S') &&
            (module_id[id_len - 2] == L'c' || module_id[id_len - 2] == L'C') &&
            (module_id[id_len - 1] == L'm' || module_id[id_len - 1] == L'M')) {
            module_id[id_len - 4] = L'\0';
        }
    }

    return module_id;
}


// 提取token对应的源码文本，返回新分配的以L'\0'结尾的宽字符串（使用系统malloc）。
// 调用者负责free。虚拟token（index == SIZE_MAX）返回NULL，但begin虚拟token返回L"begin"。
static wchar_t *am_token_text_dup(am_token_t *tok, wchar_t *code) {
    if (!tok) return NULL;

    // 虚拟token：仅begin返回对应文本，其他返回NULL
    if (tok->index == SIZE_MAX) {
        if (tok->type == AM_TOKEN_TYPE_KEYWORD && tok->length == 5) {
            wchar_t *buf = (wchar_t *)malloc(6 * sizeof(wchar_t));
            if (!buf) return NULL;
            wcscpy(buf, L"begin");
            return buf;
        }
        return NULL;
    }

    if (!code) return NULL;

    size_t len = tok->length;
    wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!buf) return NULL;

    wcsncpy(buf, &code[tok->index], len);
    buf[len] = L'\0';
    return buf;
}


// 创建宽字符串对象。
// 注意：am_wstring_t.content是am_value_t数组，每个元素是一个am_wchar_t。
static am_wstring_t *am_wstring_create(am_allocator_t *alloc, const wchar_t *str, size_t len) {
    if (!str) return NULL;

    am_wstring_t *ws = (am_wstring_t *)am_malloc(alloc, sizeof(am_wstring_t) + len * sizeof(am_value_t));
    if (!ws) return NULL;

    ws->base.type = AM_OBJECT_TYPE_WSTRING;
    ws->length = len;
    for (size_t i = 0; i < len; i++) {
        ws->content[i] = am_make_value_of_wchar((am_wchar_t)str[i]);
    }
    return ws;
}


// 根据token文本查找关键字在AM_KEYWORDS中的索引。
static size_t keyword_index(const wchar_t *text) {
    for (size_t i = 0; AM_KEYWORDS[i]; i++) {
        if (wcscmp(text, AM_KEYWORDS[i]) == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}


// 辅助：从am_value_t解包出am_list_t*（不做类型检查，调用者应确保）
static am_list_t *value_to_list(am_value_t v) {
    return (am_list_t *)am_value_to_ptr(v);
}


// ===============================================================================
// 构造函数 / 析构函数 / 拷贝
// ===============================================================================

// 功能描述：创建AST对象。
am_ast_t *am_ast_create(am_allocator_t *alloc, wchar_t *code, wchar_t *absolute_path, am_token_t *tokens, size_t token_count) {
    if (!alloc) return NULL;

    am_ast_t *ast = (am_ast_t *)am_calloc(alloc, sizeof(am_ast_t));
    if (!ast) return NULL;

    ast->alloc = alloc;
    ast->code = code;
    ast->tokens = tokens;
    ast->token_count = token_count;
    ast->absolute_path = absolute_path;
    ast->module_id = path_to_module_id(alloc, absolute_path);
    if (!ast->module_id) {
        am_free(alloc, ast);
        return NULL;
    }

    ast->symbol_vocab = am_vocab_create(alloc, 64);
    ast->var_vocab = am_vocab_create(alloc, 64);
    ast->var_type = am_list_create(alloc, 64, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    ast->nodes = am_heap_create(alloc, 1024);
    ast->node_token_mapping = am_map_create(alloc, 64);
    ast->scopes = am_map_create(alloc, 64);
    ast->var_arn_mapping = am_map_create(alloc, 64);
    ast->lambda_handles = am_list_create(alloc, 32, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    ast->tailcall_handles = am_list_create(alloc, 32, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    ast->var_top = am_list_create(alloc, 32, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    ast->dependencies = am_map_create(alloc, 16);
    ast->natives = am_map_create(alloc, 16);

    if (!ast->symbol_vocab || !ast->var_vocab || !ast->var_type || !ast->nodes ||
        !ast->node_token_mapping || !ast->scopes || !ast->var_arn_mapping ||
        !ast->lambda_handles || !ast->tailcall_handles || !ast->var_top ||
        !ast->dependencies || !ast->natives) {
        am_ast_destroy(ast);
        return NULL;
    }

    return ast;
}


// 功能描述：销毁AST对象。
int32_t am_ast_destroy(am_ast_t *ast) {
    if (!ast) return 0;

    am_allocator_t *alloc = ast->alloc;

    if (ast->module_id) am_free(alloc, ast->module_id);
    if (ast->symbol_vocab) am_vocab_destroy(alloc, ast->symbol_vocab);
    if (ast->var_vocab) am_vocab_destroy(alloc, ast->var_vocab);
    if (ast->var_type) am_list_destroy(alloc, ast->var_type);
    if (ast->nodes) am_heap_destroy(alloc, ast->nodes);
    if (ast->node_token_mapping) am_map_destroy(alloc, ast->node_token_mapping);
    if (ast->scopes) am_map_destroy(alloc, ast->scopes);
    if (ast->var_arn_mapping) am_map_destroy(alloc, ast->var_arn_mapping);
    if (ast->lambda_handles) am_list_destroy(alloc, ast->lambda_handles);
    if (ast->tailcall_handles) am_list_destroy(alloc, ast->tailcall_handles);
    if (ast->var_top) am_list_destroy(alloc, ast->var_top);
    if (ast->dependencies) am_map_destroy(alloc, ast->dependencies);
    if (ast->natives) am_map_destroy(alloc, ast->natives);

    am_free(alloc, ast);
    return 0;
}


// 功能描述：深拷贝AST对象。
am_ast_t *am_ast_copy(am_ast_t *ast) {
    if (!ast) return NULL;

    am_ast_t *copy = (am_ast_t *)am_calloc(ast->alloc, sizeof(am_ast_t));
    if (!copy) return NULL;

    copy->alloc = ast->alloc;
    copy->code = ast->code;
    copy->tokens = ast->tokens;
    copy->token_count = ast->token_count;
    copy->absolute_path = ast->absolute_path;
    copy->module_id = path_to_module_id(ast->alloc, ast->absolute_path);
    if (!copy->module_id) {
        am_free(ast->alloc, copy);
        return NULL;
    }

    copy->symbol_vocab = ast->symbol_vocab ? am_vocab_copy(ast->alloc, ast->symbol_vocab) : NULL;
    copy->var_vocab = ast->var_vocab ? am_vocab_copy(ast->alloc, ast->var_vocab) : NULL;
    copy->var_type = ast->var_type ? am_list_copy(ast->alloc, ast->var_type) : NULL;
    copy->nodes = ast->nodes ? am_heap_copy(ast->alloc, ast->nodes) : NULL;
    copy->node_token_mapping = ast->node_token_mapping ? am_map_copy(ast->alloc, ast->node_token_mapping) : NULL;
    copy->scopes = ast->scopes ? am_map_copy(ast->alloc, ast->scopes) : NULL;
    copy->var_arn_mapping = ast->var_arn_mapping ? am_map_copy(ast->alloc, ast->var_arn_mapping) : NULL;
    copy->lambda_handles = ast->lambda_handles ? am_list_copy(ast->alloc, ast->lambda_handles) : NULL;
    copy->tailcall_handles = ast->tailcall_handles ? am_list_copy(ast->alloc, ast->tailcall_handles) : NULL;
    copy->var_top = ast->var_top ? am_list_copy(ast->alloc, ast->var_top) : NULL;
    copy->dependencies = ast->dependencies ? am_map_copy(ast->alloc, ast->dependencies) : NULL;
    copy->natives = ast->natives ? am_map_copy(ast->alloc, ast->natives) : NULL;

    if (!copy->symbol_vocab || !copy->var_vocab || !copy->var_type || !copy->nodes ||
        !copy->node_token_mapping || !copy->scopes || !copy->var_arn_mapping ||
        !copy->lambda_handles || !copy->tailcall_handles || !copy->var_top ||
        !copy->dependencies || !copy->natives) {
        am_ast_destroy(copy);
        return NULL;
    }

    return copy;
}


// 功能描述：设置AST节点把柄对应的token索引。
int32_t am_ast_set_node_token_index(am_ast_t *ast, am_handle_t node_handle, size_t token_index) {
    if (!ast || !ast->node_token_mapping) return -1;
    am_map_t *map = am_map_set(ast->alloc, ast->node_token_mapping,
                                am_make_value_of_handle(node_handle),
                                am_make_value_of_uint((am_uint_t)token_index));
    if (!map) return -1;
    ast->node_token_mapping = map;
    return 0;
}


// 功能描述：获取AST节点把柄对应的token索引。
size_t am_ast_get_node_token_index(am_ast_t *ast, am_handle_t node_handle) {
    if (!ast || !ast->node_token_mapping) return SIZE_MAX;
    am_value_t v = am_map_get(ast->alloc, ast->node_token_mapping, am_make_value_of_handle(node_handle));
    if (am_value_is_uint(v)) {
        return (size_t)am_value_to_uint(v);
    }
    return SIZE_MAX;
}


// 功能描述：融合另一个AST。
// 注意：此实现当前假设 am_heap_set 能够为未分配的把柄创建条目。但 heap 已改为严格模式，
//       把柄必须先通过 am_heap_alloc_handle 申请，不允许直接创建/注册指定把柄。因此该函数
//       需要后续改造（例如深拷贝源 AST 节点并在 target 中重新申请把柄、重映射内部引用）。
int32_t am_ast_merge(am_ast_t *target, am_ast_t *source, const wchar_t *order) {
    if (!target || !source || !order) return 0;
    int top_order = (wcscmp(order, L"top") == 0);
    int bottom_order = (wcscmp(order, L"bottom") == 0);
    if (!top_order && !bottom_order) return 0;

    // 1. 融合 nodes：将source的所有节点按位拷贝到target（与TS当前行为一致；TODO 建议深拷贝）
    size_t source_node_count = am_map_length(source->alloc, source->nodes->table);
    am_value_t *source_handles = am_map_keys(source->alloc, source->nodes->table);
    if (source_node_count > 0 && !source_handles) return 0;
    for (size_t i = 0; i < source_node_count; i++) {
        am_handle_t hd = am_value_to_handle(source_handles[i]);
        am_value_t v = am_heap_get(source->alloc, source->nodes, hd);
        // 将source的对象指针也设置到target的同一把柄下
        am_heap_set(target->alloc, target->nodes, hd, v);
    }
    free(source_handles);

    // 2. 重组全局节点
    am_handle_t source_top_lambda = am_ast_get_top_lambda_node_handle(source);
    am_handle_t target_top_lambda = am_ast_get_top_lambda_node_handle(target);
    if (source_top_lambda == AM_HANDLE_NULL || target_top_lambda == AM_HANDLE_NULL) return 0;

    am_value_t *source_bodies = am_ast_get_global_nodes(source);
    am_value_t *target_bodies = am_ast_get_global_nodes(target);
    if (!source_bodies || !target_bodies) {
        free(source_bodies);
        free(target_bodies);
        return 0;
    }

    size_t source_n_body = am_list_lambda_get_body_number(source->alloc,
        value_to_list(am_heap_get(source->alloc, source->nodes, source_top_lambda)));
    size_t target_n_body = am_list_lambda_get_body_number(target->alloc,
        value_to_list(am_heap_get(target->alloc, target->nodes, target_top_lambda)));

    size_t new_n_body = source_n_body + target_n_body;
    am_value_t *new_bodies = (am_value_t *)malloc(new_n_body * sizeof(am_value_t));
    if (!new_bodies) {
        free(source_bodies);
        free(target_bodies);
        return 0;
    }

    if (top_order) {
        memcpy(new_bodies, source_bodies, source_n_body * sizeof(am_value_t));
        memcpy(new_bodies + source_n_body, target_bodies, target_n_body * sizeof(am_value_t));
    }
    else {
        memcpy(new_bodies, target_bodies, target_n_body * sizeof(am_value_t));
        memcpy(new_bodies + target_n_body, source_bodies, source_n_body * sizeof(am_value_t));
    }

    if (am_ast_set_global_nodes(target, new_bodies, new_n_body) < 0) {
        free(new_bodies);
        free(source_bodies);
        free(target_bodies);
        return 0;
    }

    // 修改被挂载节点的parent字段（仅对handle类型的子节点有效）
    for (size_t i = 0; i < source_n_body; i++) {
        if (!am_value_is_handle(source_bodies[i])) continue;
        am_value_t node_val = am_heap_get(target->alloc, target->nodes, am_value_to_handle(source_bodies[i]));
        if (am_value_is_ptr(node_val)) {
            am_list_t *lst = value_to_list(node_val);
            lst->parent = target_top_lambda;
        }
    }

    free(new_bodies);
    free(source_bodies);
    free(target_bodies);

    // 3. 删除source原来的顶级App和顶级Lambda节点
    am_handle_t source_top_app = am_ast_get_top_node_handle(source);
    if (source_top_app != AM_HANDLE_NULL) {
        am_heap_free_handle(target->alloc, target->nodes, source_top_app);
    }
    if (source_top_lambda != AM_HANDLE_NULL) {
        am_heap_free_handle(target->alloc, target->nodes, source_top_lambda);
    }

    // 4. 合并 node_token_mapping
    size_t ntm_count = am_map_length(source->alloc, source->node_token_mapping);
    am_value_t *ntm_keys = am_map_keys(source->alloc, source->node_token_mapping);
    if (ntm_count > 0 && !ntm_keys) return 0;
    for (size_t i = 0; i < ntm_count; i++) {
        am_value_t v = am_map_get(source->alloc, source->node_token_mapping, ntm_keys[i]);
        am_map_t *map = am_map_set(target->alloc, target->node_token_mapping, ntm_keys[i], v);
        if (!map) { free(ntm_keys); return 0; }
        target->node_token_mapping = map;
    }
    free(ntm_keys);

    // 5. 合并 lambda_handles（去掉source的顶级lambda）
    for (size_t i = 0; i < source->lambda_handles->length; i++) {
        am_value_t h = am_list_get(source->alloc, source->lambda_handles, i);
        if (am_value_to_handle(h) == source_top_lambda) continue;
        am_list_t *lst = am_list_push(target->alloc, target->lambda_handles, h);
        if (!lst) return 0;
        target->lambda_handles = lst;
    }

    // 6. 合并 tailcall_handles（去掉source的顶级app）
    for (size_t i = 0; i < source->tailcall_handles->length; i++) {
        am_value_t h = am_list_get(source->alloc, source->tailcall_handles, i);
        if (am_value_to_handle(h) == source_top_app) continue;
        am_list_t *lst = am_list_push(target->alloc, target->tailcall_handles, h);
        if (!lst) return 0;
        target->tailcall_handles = lst;
    }

    // 7. 合并 var_arn_mapping、var_top、dependencies、natives
    size_t vm_count = am_map_length(source->alloc, source->var_arn_mapping);
    am_value_t *vm_keys = am_map_keys(source->alloc, source->var_arn_mapping);
    if (vm_count > 0 && !vm_keys) return 0;
    for (size_t i = 0; i < vm_count; i++) {
        am_value_t v = am_map_get(source->alloc, source->var_arn_mapping, vm_keys[i]);
        am_map_t *map = am_map_set(target->alloc, target->var_arn_mapping, vm_keys[i], v);
        if (!map) { free(vm_keys); return 0; }
        target->var_arn_mapping = map;
    }
    free(vm_keys);

    for (size_t i = 0; i < source->var_top->length; i++) {
        am_value_t v = am_list_get(source->alloc, source->var_top, i);
        am_list_t *lst = am_list_push(target->alloc, target->var_top, v);
        if (!lst) return 0;
        target->var_top = lst;
    }

    size_t dep_count = am_map_length(source->alloc, source->dependencies);
    am_value_t *dep_keys = am_map_keys(source->alloc, source->dependencies);
    if (dep_count > 0 && !dep_keys) return 0;
    for (size_t i = 0; i < dep_count; i++) {
        am_value_t v = am_map_get(source->alloc, source->dependencies, dep_keys[i]);
        am_map_t *map = am_map_set(target->alloc, target->dependencies, dep_keys[i], v);
        if (!map) { free(dep_keys); return 0; }
        target->dependencies = map;
    }
    free(dep_keys);

    size_t native_count = am_map_length(source->alloc, source->natives);
    am_value_t *native_keys = am_map_keys(source->alloc, source->natives);
    if (native_count > 0 && !native_keys) return 0;
    for (size_t i = 0; i < native_count; i++) {
        am_value_t v = am_map_get(source->alloc, source->natives, native_keys[i]);
        am_map_t *map = am_map_set(target->alloc, target->natives, native_keys[i], v);
        if (!map) { free(native_keys); return 0; }
        target->natives = map;
    }
    free(native_keys);

    return 1;
}


// ===============================================================================
// 词汇表构建
// ===============================================================================

// 功能描述：遍历tokens，使用其中的KEYWORD和SYMBOL构建ast->symbol_vocab。
size_t am_build_symbol_vocabulary(am_ast_t *ast) {
    if (!ast || !ast->symbol_vocab || !ast->tokens) return 0;

    // 预置24个关键字到symbol_vocab的前24个条目
    for (size_t i = 0; i < AM_KEYWORDS_NUM && AM_KEYWORDS[i]; i++) {
        size_t idx = am_vocab_insert(ast->alloc, ast->symbol_vocab, (wchar_t *)AM_KEYWORDS[i]);
        if (idx == SIZE_MAX) return 0;
    }

    for (size_t i = 0; i < ast->token_count; i++) {
        am_token_t *t = &ast->tokens[i];
        // if (t->index == SIZE_MAX) continue; // 跳过虚拟token

        if (t->type == AM_TOKEN_TYPE_KEYWORD) {
            wchar_t *text = am_token_text_dup(t, ast->code);
            if (!text) return 0;
            size_t kw_idx = keyword_index(text);
            free(text);
            if (kw_idx != SIZE_MAX) {
                t->id = kw_idx;
            }
        }
        else if (t->type == AM_TOKEN_TYPE_SYMBOL) {
            wchar_t *text = am_token_text_dup(t, ast->code);
            if (!text) return 0;
            size_t idx = am_vocab_insert(ast->alloc, ast->symbol_vocab, text);
            free(text);
            if (idx == SIZE_MAX) return 0;
            t->id = idx;
        }
    }

    return ast->symbol_vocab->length;
}


// 功能描述：遍历tokens，使用其中的IDENTIFIER构建ast->var_vocab。
size_t am_build_variable_vocabulary(am_ast_t *ast) {
    if (!ast || !ast->var_vocab || !ast->var_type || !ast->tokens) return 0;

    for (size_t i = 0; i < ast->token_count; i++) {
        am_token_t *t = &ast->tokens[i];
        if (t->index == SIZE_MAX) continue; // 跳过虚拟token

        if (t->type == AM_TOKEN_TYPE_IDENTIFIER) {
            wchar_t *text = am_token_text_dup(t, ast->code);
            if (!text) return 0;
            size_t old_len = ast->var_vocab->length;
            size_t idx = am_vocab_insert(ast->alloc, ast->var_vocab, text);
            free(text);
            if (idx == SIZE_MAX) return 0;
            // 新变量加入时，同步在 var_type 中追加默认类型
            if (idx == old_len) {
                am_list_t *vt = am_list_push(ast->alloc, ast->var_type,
                                              am_make_value_of_uint(AM_VAR_TYPE_OLD));
                if (!vt) return 0;
                ast->var_type = vt;
            }
            t->id = idx;
        }
    }

    return ast->var_vocab->length;
}


// ===============================================================================
// EXT/NATIVE/IMPORT 引用判断
// ===============================================================================

// 功能描述：判断某个变量在形式上是否是“前缀.后缀”的格式（EXT_REF）。
int32_t am_ast_check_ext_ref(am_ast_t *ast, am_varid_t v) {
    if (!ast || !ast->var_vocab) return -1;

    wchar_t *var_str = am_vocab_get(ast->alloc, ast->var_vocab, &v);
    if (!var_str) return -1;

    // 必须有且仅有一个点号，且不在开头和末尾
    wchar_t *first_dot = wcschr(var_str, L'.');
    if (!first_dot || first_dot == var_str || first_dot[1] == L'\0') return -1;
    if (wcschr(first_dot + 1, L'.')) return -1;

    return 0;
}


// 功能描述：判断某个变量是否是 AM_VAR_TYPE_NATIVE_REF。
int32_t am_ast_check_native_ref(am_ast_t *ast, am_varid_t v) {
    if (!ast || !ast->var_vocab || !ast->natives) return -1;

    wchar_t *var_str = am_vocab_get(ast->alloc, ast->var_vocab, &v);
    if (!var_str) return -1;

    // 提取点号分隔的第1部分
    size_t len = wcslen(var_str);
    wchar_t *prefix = (wchar_t *)am_malloc(ast->alloc, (len + 1) * sizeof(wchar_t));
    if (!prefix) return -1;

    size_t i = 0;
    while (i < len && var_str[i] != L'.') {
        prefix[i] = var_str[i];
        i++;
    }
    prefix[i] = L'\0';

    size_t native_varid = am_vocab_find(ast->alloc, ast->var_vocab, prefix);
    am_free(ast->alloc, prefix);
    if (native_varid == SIZE_MAX) return -1;

    return am_map_contains(ast->alloc, ast->natives, am_make_value_of_varid(native_varid));
}


// 功能描述：判断某个变量是否是 AM_VAR_TYPE_IMPORT_REF。
int32_t am_ast_check_import_ref(am_ast_t *ast, am_varid_t v) {
    if (!ast || !ast->var_vocab || !ast->dependencies) return -1;

    wchar_t *var_str = am_vocab_get(ast->alloc, ast->var_vocab, &v);
    if (!var_str) return -1;

    // 提取最后一个点号分隔的第1部分作为 alias
    wchar_t *last_dot = wcsrchr(var_str, L'.');
    if (!last_dot || last_dot == var_str) return -1;

    size_t prefix_len = (size_t)(last_dot - var_str);
    wchar_t *prefix = (wchar_t *)am_malloc(ast->alloc, (prefix_len + 1) * sizeof(wchar_t));
    if (!prefix) return -1;
    wcsncpy(prefix, var_str, prefix_len);
    prefix[prefix_len] = L'\0';

    size_t alias_varid = am_vocab_find(ast->alloc, ast->var_vocab, prefix);
    am_free(ast->alloc, prefix);
    if (alias_varid == SIZE_MAX) return -1;

    return am_map_contains(ast->alloc, ast->dependencies, am_make_value_of_varid(alias_varid));
}


// ===============================================================================
// 节点访问
// ===============================================================================

// 功能描述：根据把柄从AST->nodes堆中获取相应的am_value_t。
am_value_t am_ast_get_node(am_ast_t *ast, am_handle_t handle) {
    if (!ast || !ast->nodes) return AM_VALUE_UNDEFINED;
    return am_heap_get(ast->alloc, ast->nodes, handle);
}


// ===============================================================================
// 节点创建
// ===============================================================================

// 功能描述：创建lambda对象，返回其在AST->nodes堆中的把柄。
am_handle_t am_ast_make_lambda_node(am_ast_t *ast, am_handle_t parent) {
    if (!ast || !ast->nodes) return AM_HANDLE_NULL;

    am_handle_t handle = am_heap_alloc_handle(ast->alloc, ast->nodes);
    if (handle == AM_HANDLE_NULL) return AM_HANDLE_NULL;

    am_list_t *lambda = am_list_create(ast->alloc, 32, AM_LIST_TYPE_LAMBDA, parent);
    if (!lambda) {
        am_heap_free_handle(ast->alloc, ast->nodes, handle);
        return AM_HANDLE_NULL;
    }

    // Lambda表结构：children[0] = 'lambda, children[1] = 参数数量(uint)
    lambda = am_list_push(ast->alloc, lambda, AM_VALUE_KW_lambda);
    if (!lambda) {
        am_heap_free_handle(ast->alloc, ast->nodes, handle);
        return AM_HANDLE_NULL;
    }
    lambda = am_list_push(ast->alloc, lambda, am_make_value_of_uint(0));
    if (!lambda) {
        am_heap_free_handle(ast->alloc, ast->nodes, handle);
        return AM_HANDLE_NULL;
    }

    if (am_heap_set(ast->alloc, ast->nodes, handle, am_make_value_of_ptr((am_object_t *)lambda)) != 0) {
        am_list_destroy(ast->alloc, lambda);
        am_heap_free_handle(ast->alloc, ast->nodes, handle);
        return AM_HANDLE_NULL;
    }

    am_list_t *lst = am_list_push(ast->alloc, ast->lambda_handles, am_make_value_of_handle(handle));
    if (!lst) {
        am_heap_free_handle(ast->alloc, ast->nodes, handle);
        return AM_HANDLE_NULL;
    }
    ast->lambda_handles = lst;

    return handle;
}


// 功能描述：创建SList对象，返回其在AST->nodes堆中的把柄。
am_handle_t am_ast_make_slist_node(am_ast_t *ast, am_handle_t parent, int32_t type) {
    if (!ast || !ast->nodes) return AM_HANDLE_NULL;
    if (type != AM_LIST_TYPE_APPLICATION && type != AM_LIST_TYPE_QUOTE &&
        type != AM_LIST_TYPE_QUASIQUOTE && type != AM_LIST_TYPE_UNQUOTE) {
        return AM_HANDLE_NULL;
    }

    am_handle_t handle = am_heap_alloc_handle(ast->alloc, ast->nodes);
    if (handle == AM_HANDLE_NULL) return AM_HANDLE_NULL;

    am_list_t *lst = am_list_create(ast->alloc, 32, type, parent);
    if (!lst) {
        am_heap_free_handle(ast->alloc, ast->nodes, handle);
        return AM_HANDLE_NULL;
    }

    if (am_heap_set(ast->alloc, ast->nodes, handle, am_make_value_of_ptr((am_object_t *)lst)) != 0) {
        am_list_destroy(ast->alloc, lst);
        am_heap_free_handle(ast->alloc, ast->nodes, handle);
        return AM_HANDLE_NULL;
    }

    return handle;
}


// 功能描述：创建WString对象，返回其在AST->nodes堆中的把柄。
am_handle_t am_ast_make_wstring_node(am_ast_t *ast, am_token_t *str_token) {
    if (!ast || !ast->nodes || !str_token) return AM_HANDLE_NULL;

    am_handle_t handle = am_heap_alloc_handle(ast->alloc, ast->nodes);
    if (handle == AM_HANDLE_NULL) return AM_HANDLE_NULL;

    // 从token指示的位置截取字符串（包含引号，与TS行为一致）
    size_t len = str_token->length;
    wchar_t *text = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!text) {
        am_heap_free_handle(ast->alloc, ast->nodes, handle);
        return AM_HANDLE_NULL;
    }
    wcsncpy(text, &ast->code[str_token->index], len);
    text[len] = L'\0';

    am_wstring_t *ws = am_wstring_create(ast->alloc, text, len);
    free(text);
    if (!ws) {
        am_heap_free_handle(ast->alloc, ast->nodes, handle);
        return AM_HANDLE_NULL;
    }

    if (am_heap_set(ast->alloc, ast->nodes, handle, am_make_value_of_ptr((am_object_t *)ws)) != 0) {
        // 注：am_wstring_t 的 content 是柔性数组，am_free 即可释放整个对象
        am_free(ast->alloc, ws);
        am_heap_free_handle(ast->alloc, ast->nodes, handle);
        return AM_HANDLE_NULL;
    }

    return handle;
}


// ===============================================================================
// 顶级节点操作
// ===============================================================================

typedef struct {
    am_handle_t found_handle;
} am_top_node_search_t;

static void am_ast_top_node_iter(am_handle_t handle, am_value_t value, void *user_data) {
    am_top_node_search_t *ctx = (am_top_node_search_t *)user_data;
    if (ctx->found_handle != AM_HANDLE_NULL) return;
    if (!am_value_is_ptr(value)) return;

    am_object_t *obj = am_value_to_ptr(value);
    if (obj->type != AM_OBJECT_TYPE_LIST) return;

    am_list_t *lst = (am_list_t *)obj;
    if (lst->parent == AM_TOP_NODE_HANDLE) {
        ctx->found_handle = handle;
    }
}


// 功能描述：查找最顶级Application的handle。
am_handle_t am_ast_get_top_node_handle(am_ast_t *ast) {
    if (!ast || !ast->nodes) return AM_HANDLE_NULL;

    am_top_node_search_t ctx = { AM_HANDLE_NULL };
    am_heap_iter(ast->alloc, ast->nodes, am_ast_top_node_iter, &ctx);
    return ctx.found_handle;
}


// 功能描述：查找顶级Lambda（全局作用域）节点的handle。
am_handle_t am_ast_get_top_lambda_node_handle(am_ast_t *ast) {
    am_handle_t top_app = am_ast_get_top_node_handle(ast);
    if (top_app == AM_HANDLE_NULL) return AM_HANDLE_NULL;

    am_value_t app_val = am_heap_get(ast->alloc, ast->nodes, top_app);
    if (!am_value_is_ptr(app_val)) return AM_HANDLE_NULL;

    am_list_t *app = (am_list_t *)am_value_to_ptr(app_val);
    if (app->length == 0) return AM_HANDLE_NULL;

    am_value_t first = am_list_get(ast->alloc, app, 0);
    if (!am_value_is_handle(first)) return AM_HANDLE_NULL;

    return am_value_to_handle(first);
}


// 功能描述：获取位于全局作用域的node列表（函数体列表）。
am_value_t *am_ast_get_global_nodes(am_ast_t *ast) {
    if (!ast || !ast->nodes) return NULL;

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    if (top_lambda == AM_HANDLE_NULL) return NULL;

    am_value_t lambda_val = am_heap_get(ast->alloc, ast->nodes, top_lambda);
    if (!am_value_is_ptr(lambda_val)) return NULL;

    am_list_t *lambda = (am_list_t *)am_value_to_ptr(lambda_val);
    size_t n_body = 0;
    return am_list_lambda_get_bodies(ast->alloc, lambda, &n_body);
}


// 功能描述：设置全局作用域（顶层lambda）的node列表（函数体列表）。
int32_t am_ast_set_global_nodes(am_ast_t *ast, am_value_t *bodies, size_t n_body) {
    if (!ast || !ast->nodes || !bodies) return -1;

    am_handle_t top_lambda = am_ast_get_top_lambda_node_handle(ast);
    if (top_lambda == AM_HANDLE_NULL) return -1;

    am_value_t lambda_val = am_heap_get(ast->alloc, ast->nodes, top_lambda);
    if (!am_value_is_ptr(lambda_val)) return -1;

    am_list_t *lambda = (am_list_t *)am_value_to_ptr(lambda_val);

    am_list_t *new_lambda = am_list_lambda_set_bodies(ast->alloc, lambda, bodies, &n_body);
    if (!new_lambda) return -1;

    // 如果lambda对象指针发生变化，更新heap中的绑定
    if (new_lambda != lambda) {
        if (am_heap_set(ast->alloc, ast->nodes, top_lambda, am_make_value_of_ptr((am_object_t *)new_lambda)) != 0) {
            am_list_destroy(ast->alloc, new_lambda);
            return -1;
        }
    }

    return 0;
}


// ===============================================================================
// 作用域上溯查找
// ===============================================================================

// 功能描述：从某个节点开始，向上上溯查找某个varid归属的lambda节点把柄。
am_handle_t am_ast_find_var_lambda_handle(am_ast_t *ast, am_varid_t varid, am_handle_t from_node_handle) {
    if (!ast || !ast->nodes) return AM_HANDLE_NULL;

    am_handle_t current = from_node_handle;
    while (current != AM_TOP_NODE_HANDLE) {
        am_value_t node_val = am_heap_get(ast->alloc, ast->nodes, current);
        if (!am_value_is_ptr(node_val)) return AM_HANDLE_NULL;

        am_object_t *obj = am_value_to_ptr(node_val);
        if (obj->type == AM_OBJECT_TYPE_LIST) {
            am_list_t *lst = (am_list_t *)obj;
            if (lst->type == AM_LIST_TYPE_LAMBDA) {
                // 获取该lambda对应的scope
                am_value_t scope_handle_val = am_map_get(ast->alloc, ast->scopes, am_make_value_of_handle(current));
                if (am_value_is_handle(scope_handle_val)) {
                    am_handle_t scope_handle = am_value_to_handle(scope_handle_val);
                    am_value_t scope_val = am_heap_get(ast->alloc, ast->nodes, scope_handle);
                    if (am_value_is_ptr(scope_val)) {
                        am_scope_t *scope = (am_scope_t *)am_value_to_ptr(scope_val);
                        if (am_scope_has_var(ast->alloc, scope, varid) >= 0) {
                            return current;
                        }
                    }
                }
            }
            current = lst->parent;
        }
        else {
            // 非list节点无法继续上溯
            return AM_HANDLE_NULL;
        }
    }

    return AM_HANDLE_NULL;
}


// 功能描述：从某个节点开始，向上上溯查找最近的lambda节点的把柄。
am_handle_t am_ast_find_nearest_lambda_handle(am_ast_t *ast, am_handle_t from_node_handle) {
    if (!ast || !ast->nodes) return AM_HANDLE_NULL;

    am_handle_t current = from_node_handle;
    while (current != AM_TOP_NODE_HANDLE) {
        am_value_t node_val = am_heap_get(ast->alloc, ast->nodes, current);
        if (!am_value_is_ptr(node_val)) return AM_HANDLE_NULL;

        am_object_t *obj = am_value_to_ptr(node_val);
        if (obj->type == AM_OBJECT_TYPE_LIST) {
            am_list_t *lst = (am_list_t *)obj;
            if (lst->type == AM_LIST_TYPE_LAMBDA) {
                return current;
            }
            current = lst->parent;
        }
        else {
            return AM_HANDLE_NULL;
        }
    }

    return AM_HANDLE_NULL;
}


// ===============================================================================
// 变量换名
// ===============================================================================

// 功能描述：生成模块（AST）内唯一的变量名。
am_varid_t am_ast_make_unique_variable(am_ast_t *ast, am_varid_t varid, am_handle_t lambda_handle) {
    if (!ast || !ast->var_vocab || !ast->var_type) return SIZE_MAX;

    wchar_t *var_str = am_vocab_get(ast->alloc, ast->var_vocab, &varid);
    if (!var_str) return SIZE_MAX;

    // 生成新变量名：module_id.lambda_handle.var_string
    // 估算所需空间：module_id + 分隔点1 + handle最大20位 + 分隔点1 + var_str + 结尾\0
    size_t module_id_len = wcslen(ast->module_id);
    size_t var_len = wcslen(var_str);
    size_t buf_size = module_id_len + 1 + 20 + 1 + var_len + 1;

    wchar_t *new_name = (wchar_t *)am_malloc(ast->alloc, buf_size * sizeof(wchar_t));
    if (!new_name) return SIZE_MAX;

    int n = swprintf(new_name, buf_size, L"%ls.%zu.%ls", ast->module_id, lambda_handle, var_str);
    if (n <= 0 || (size_t)n >= buf_size) {
        am_free(ast->alloc, new_name);
        return SIZE_MAX;
    }

    size_t old_len = ast->var_vocab->length;
    size_t new_varid = am_vocab_insert(ast->alloc, ast->var_vocab, new_name);
    am_free(ast->alloc, new_name);

    if (new_varid == SIZE_MAX) return SIZE_MAX;
    // 新变量加入时，同步在 var_type 中追加默认类型
    if (new_varid == old_len) {
        am_list_t *vt = am_list_push(ast->alloc, ast->var_type,
                                      am_make_value_of_uint(AM_VAR_TYPE_NEW));
        if (!vt) return SIZE_MAX;
        ast->var_type = vt;
    }
    return (am_varid_t)new_varid;
}


// 功能描述：为 import 别名生成模块级唯一变量名。
am_varid_t am_ast_make_unique_module_alias(am_ast_t *ast, am_varid_t alias_varid) {
    if (!ast || !ast->var_vocab || !ast->var_type) return SIZE_MAX;

    wchar_t *alias_str = am_vocab_get(ast->alloc, ast->var_vocab, &alias_varid);
    if (!alias_str) return SIZE_MAX;

    // 生成新变量名：module_id.alias
    size_t module_id_len = wcslen(ast->module_id);
    size_t alias_len = wcslen(alias_str);
    size_t buf_size = module_id_len + 1 + alias_len + 1;

    wchar_t *new_name = (wchar_t *)am_malloc(ast->alloc, buf_size * sizeof(wchar_t));
    if (!new_name) return SIZE_MAX;

    int n = swprintf(new_name, buf_size, L"%ls.%ls", ast->module_id, alias_str);
    if (n <= 0 || (size_t)n >= buf_size) {
        am_free(ast->alloc, new_name);
        return SIZE_MAX;
    }

    size_t old_len = ast->var_vocab->length;
    size_t new_varid = am_vocab_insert(ast->alloc, ast->var_vocab, new_name);
    am_free(ast->alloc, new_name);

    if (new_varid == SIZE_MAX) return SIZE_MAX;

    // 设置 var_type 为 AM_VAR_TYPE_IMPORT_ALIAS
    if (new_varid == old_len) {
        am_list_t *vt = am_list_push(ast->alloc, ast->var_type,
                                      am_make_value_of_uint(AM_VAR_TYPE_IMPORT_ALIAS));
        if (!vt) return SIZE_MAX;
        ast->var_type = vt;
    }
    else {
        if (am_list_set(ast->alloc, ast->var_type, new_varid,
                        am_make_value_of_uint(AM_VAR_TYPE_IMPORT_ALIAS)) != 0) {
            return SIZE_MAX;
        }
    }

    return (am_varid_t)new_varid;
}


// 功能描述：为 import 外部引用生成模块级唯一变量名。
am_varid_t am_ast_make_unique_import_ref(am_ast_t *ast, am_varid_t import_ref_varid) {
    if (!ast || !ast->var_vocab || !ast->var_type) return SIZE_MAX;

    wchar_t *ref_str = am_vocab_get(ast->alloc, ast->var_vocab, &import_ref_varid);
    if (!ref_str) return SIZE_MAX;

    // 生成新变量名：module_id.import_ref
    size_t module_id_len = wcslen(ast->module_id);
    size_t ref_len = wcslen(ref_str);
    size_t buf_size = module_id_len + 1 + ref_len + 1;

    wchar_t *new_name = (wchar_t *)am_malloc(ast->alloc, buf_size * sizeof(wchar_t));
    if (!new_name) return SIZE_MAX;

    int n = swprintf(new_name, buf_size, L"%ls.%ls", ast->module_id, ref_str);
    if (n <= 0 || (size_t)n >= buf_size) {
        am_free(ast->alloc, new_name);
        return SIZE_MAX;
    }

    size_t old_len = ast->var_vocab->length;
    size_t new_varid = am_vocab_insert(ast->alloc, ast->var_vocab, new_name);
    am_free(ast->alloc, new_name);

    if (new_varid == SIZE_MAX) return SIZE_MAX;

    // 设置 var_type 为 AM_VAR_TYPE_IMPORT_REF
    if (new_varid == old_len) {
        am_list_t *vt = am_list_push(ast->alloc, ast->var_type,
                                      am_make_value_of_uint(AM_VAR_TYPE_IMPORT_REF));
        if (!vt) return SIZE_MAX;
        ast->var_type = vt;
    }
    else {
        if (am_list_set(ast->alloc, ast->var_type, new_varid,
                        am_make_value_of_uint(AM_VAR_TYPE_IMPORT_REF)) != 0) {
            return SIZE_MAX;
        }
    }

    return (am_varid_t)new_varid;
}


// ===============================================================================
// AST 基本操作辅助接口
// ===============================================================================

// 功能描述：向 tailcall_handles 中添加一个尾调用节点把柄。
int32_t am_ast_add_tailcall(am_ast_t *ast, am_handle_t handle) {
    if (!ast || !ast->tailcall_handles) return -1;
    am_list_t *lst = am_list_push(ast->alloc, ast->tailcall_handles, am_make_value_of_handle(handle));
    if (!lst) return -1;
    ast->tailcall_handles = lst;
    return 0;
}


// 功能描述：向 var_top 中添加一个顶级变量 varid。
int32_t am_ast_add_var_top(am_ast_t *ast, am_varid_t varid) {
    if (!ast || !ast->var_top) return -1;
    am_list_t *lst = am_list_push(ast->alloc, ast->var_top, am_make_value_of_varid(varid));
    if (!lst) return -1;
    ast->var_top = lst;
    return 0;
}


// 功能描述：设置依赖模块记录。
int32_t am_ast_set_dependency(am_ast_t *ast, am_varid_t alias_varid, am_handle_t path_handle) {
    if (!ast || !ast->dependencies) return -1;
    am_map_t *map = am_map_set(ast->alloc, ast->dependencies,
                                am_make_value_of_varid(alias_varid),
                                am_make_value_of_handle(path_handle));
    if (!map) return -1;
    ast->dependencies = map;
    return 0;
}


// 功能描述：设置本地库记录。
int32_t am_ast_set_native(am_ast_t *ast, am_varid_t native_varid, am_handle_t handle) {
    if (!ast || !ast->natives) return -1;
    am_map_t *map = am_map_set(ast->alloc, ast->natives,
                                am_make_value_of_varid(native_varid),
                                am_make_value_of_handle(handle));
    if (!map) return -1;
    ast->natives = map;
    return 0;
}


// 功能描述：为lambda节点设置对应的词法作用域把柄。
int32_t am_ast_set_scope(am_ast_t *ast, am_handle_t lambda_handle, am_handle_t scope_handle) {
    if (!ast || !ast->scopes) return -1;
    am_map_t *map = am_map_set(ast->alloc, ast->scopes,
                                am_make_value_of_handle(lambda_handle),
                                am_make_value_of_handle(scope_handle));
    if (!map) return -1;
    ast->scopes = map;
    return 0;
}


// 功能描述：获取lambda节点对应的词法作用域把柄。
am_handle_t am_ast_get_scope(am_ast_t *ast, am_handle_t lambda_handle) {
    if (!ast || !ast->scopes) return AM_HANDLE_NULL;
    am_value_t v = am_map_get(ast->alloc, ast->scopes, am_make_value_of_handle(lambda_handle));
    if (am_value_is_handle(v)) {
        return am_value_to_handle(v);
    }
    return AM_HANDLE_NULL;
}
