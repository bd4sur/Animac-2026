#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "linker.h"
#include "ast.h"
#include "parser.h"
#include "vocab.h"
#include "map.h"
#include "list.h"
#include "object.h"
#include "wstring.h"
#include "utils.h"


// 链接器处理的模块数上限
#define AM_LINKER_MAX_MODULES (1024)


// 链接器上下文
typedef struct am_linker_ctx_t {
    am_allocator_t *alloc;           // AST 使用的分配器
    am_ast_t *main_ast;              // 引用根模块，由调用者管理生命周期
    am_vocab_t *all_module_path;     // mod_index -> module_path
    am_ast_t **ALLAST;               // mod_index -> ast
    wchar_t **codes;                 // mod_index -> code（由 linker 读取，需释放）
    wchar_t **paths;                 // mod_index -> absolute_path（由 linker 分配，需释放）
    size_t (*DAG)[2];                // 邻接关系列表 importer_index -> importee_index
    size_t edge_num;                 // 当前边数
    size_t module_counter;           // 当前模块数
    wchar_t *base_dir;               // 基准工作目录
} am_linker_ctx_t;


// 宽字符串复制（使用系统 malloc）
static wchar_t *linker_wcsdup(const wchar_t *s) {
    if (!s) return NULL;
    size_t len = wcslen(s);
    wchar_t *dup = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!dup) return NULL;
    wcscpy(dup, s);
    return dup;
}


// 将 UTF-32 宽字符路径转换为多字节路径后读取文件内容。
// 返回系统 malloc 的 wchar_t* 源码字符串；失败返回 NULL。
static wchar_t *linker_read_file(const wchar_t *path) {
    size_t path_len = wcstombs(NULL, path, 0);
    if (path_len == (size_t)-1) return NULL;

    char *mb_path = (char *)malloc(path_len + 1);
    if (!mb_path) return NULL;
    wcstombs(mb_path, path, path_len + 1);

    wchar_t *code = read_file_to_wchar(mb_path);
    free(mb_path);
    return code;
}


// 解析 import 路径为绝对路径。
// 绝对路径（以 '/' 开头）直接返回；相对路径与 base_dir 拼接。
// 返回系统 malloc 的字符串；失败返回 NULL。
static wchar_t *linker_resolve_path(const wchar_t *base_dir, const wchar_t *path) {
    if (!path) return NULL;
    if (path[0] == L'/') {
        return linker_wcsdup(path);
    }

    size_t base_len = base_dir ? wcslen(base_dir) : 0;
    size_t path_len = wcslen(path);

    if (base_len == 0) {
        return linker_wcsdup(path);
    }

    size_t total = base_len + 1 + path_len + 1;
    wchar_t *result = (wchar_t *)malloc(total * sizeof(wchar_t));
    if (!result) return NULL;

    int n = swprintf(result, total, L"%ls/%ls", base_dir, path);
    if (n <= 0 || (size_t)n >= total) {
        free(result);
        return NULL;
    }
    return result;
}


// 从 WString 节点中提取 import 路径，去掉首尾双引号。
// 返回系统 malloc 的字符串；失败返回 NULL。
static wchar_t *linker_extract_path_from_wstring(am_wstring_t *ws) {
    if (!ws || ws->length < 2) return NULL;

    size_t len = ws->length - 2; // 去掉首尾引号
    wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!buf) return NULL;

    for (size_t i = 0; i < len; i++) {
        buf[i] = (wchar_t)am_value_to_wchar(ws->content[i + 1]);
    }
    buf[len] = L'\0';
    return buf;
}


// 创建链接器上下文。成功返回指针；失败返回 NULL。
static am_linker_ctx_t *linker_ctx_create(am_allocator_t *alloc, am_ast_t *main_ast, wchar_t *base_dir) {
    if (!alloc || !main_ast) return NULL;

    am_linker_ctx_t *ctx = (am_linker_ctx_t *)malloc(sizeof(am_linker_ctx_t));
    if (!ctx) return NULL;

    ctx->alloc = alloc;
    ctx->main_ast = main_ast;
    ctx->base_dir = base_dir;
    ctx->edge_num = 0;
    ctx->module_counter = 0;

    ctx->all_module_path = am_vocab_create(alloc, AM_LINKER_MAX_MODULES);
    ctx->ALLAST = (am_ast_t **)calloc(AM_LINKER_MAX_MODULES, sizeof(am_ast_t *));
    ctx->codes = (wchar_t **)calloc(AM_LINKER_MAX_MODULES, sizeof(wchar_t *));
    ctx->paths = (wchar_t **)calloc(AM_LINKER_MAX_MODULES, sizeof(wchar_t *));
    ctx->DAG = (size_t (*)[2])calloc(AM_LINKER_MAX_MODULES * AM_LINKER_MAX_MODULES, sizeof(size_t [2]));

    if (!ctx->all_module_path || !ctx->ALLAST || !ctx->codes || !ctx->paths || !ctx->DAG) {
        free(ctx->ALLAST);
        free(ctx->codes);
        free(ctx->paths);
        free(ctx->DAG);
        if (ctx->all_module_path) am_vocab_destroy(alloc, ctx->all_module_path);
        free(ctx);
        return NULL;
    }

    return ctx;
}


// 销毁链接器上下文。
// 注意：main_ast 及其 code/absolute_path 由调用者管理，此处不释放。
static void linker_ctx_destroy(am_linker_ctx_t *ctx) {
    if (!ctx) return;

    if (ctx->ALLAST && ctx->codes && ctx->paths) {
        for (size_t i = 0; i < ctx->module_counter; i++) {
            if (ctx->ALLAST[i] && ctx->ALLAST[i] != ctx->main_ast) {
                am_ast_destroy(ctx->ALLAST[i]);
            }
            if (ctx->codes[i]) free(ctx->codes[i]);
            if (ctx->paths[i]) free(ctx->paths[i]);
        }
    }

    if (ctx->all_module_path) am_vocab_destroy(ctx->alloc, ctx->all_module_path);
    free(ctx->ALLAST);
    free(ctx->codes);
    free(ctx->paths);
    free(ctx->DAG);
    free(ctx);
}


// 递归解析依赖模块。
// importee_path 为当前要解析模块的绝对路径；importer_index 为引用它的模块索引，SIZE_MAX 表示无引用者（根模块）。
static int32_t import_analysis(am_linker_ctx_t *ctx, wchar_t *importee_path, size_t importer_index) {
    if (!ctx || !importee_path) return -1;

    size_t current_module_index = am_vocab_find(ctx->alloc, ctx->all_module_path, importee_path);

    if (current_module_index == SIZE_MAX) {
        if (ctx->module_counter >= AM_LINKER_MAX_MODULES) return -1;
        current_module_index = ctx->module_counter;

        size_t inserted = am_vocab_insert(ctx->alloc, ctx->all_module_path, importee_path);
        if (inserted == SIZE_MAX || inserted != current_module_index) return -1;

        int is_main = (ctx->main_ast->absolute_path != NULL) &&
                      (wcscmp(importee_path, ctx->main_ast->absolute_path) == 0);

        am_ast_t *current_ast = NULL;

        if (is_main) {
            // 引用根模块直接使用调用者传入的 AST
            current_ast = ctx->main_ast;
            ctx->ALLAST[current_module_index] = current_ast;
            ctx->codes[current_module_index] = NULL;
            ctx->paths[current_module_index] = NULL;
        }
        else {
            wchar_t *path_copy = linker_wcsdup(importee_path);
            if (!path_copy) return -1;

            wchar_t *raw_code = linker_read_file(importee_path);
            if (!raw_code) {
                free(path_copy);
                return -1;
            }

            // 模块源码需包装为 ((lambda () <file_content> )) 形式
            const wchar_t *prefix = L"((lambda () ";
            const wchar_t *suffix = L" ))";
            size_t raw_len = wcslen(raw_code);
            size_t prefix_len = wcslen(prefix);
            size_t suffix_len = wcslen(suffix);
            size_t code_len = prefix_len + raw_len + suffix_len;

            wchar_t *code = (wchar_t *)malloc((code_len + 1) * sizeof(wchar_t));
            if (!code) {
                free(raw_code);
                free(path_copy);
                return -1;
            }
            size_t pos = 0;
            for (size_t i = 0; i < prefix_len; i++) code[pos++] = prefix[i];
            for (size_t i = 0; i < raw_len; i++) code[pos++] = raw_code[i];
            for (size_t i = 0; i < suffix_len; i++) code[pos++] = suffix[i];
            code[pos] = L'\0';
            free(raw_code);

            current_ast = am_parser(ctx->alloc, code, path_copy);
            if (!current_ast) {
                free(code);
                free(path_copy);
                return -1;
            }

            ctx->ALLAST[current_module_index] = current_ast;
            ctx->codes[current_module_index] = code;
            ctx->paths[current_module_index] = path_copy;
        }

        ctx->module_counter++;

        // 递归处理当前模块的依赖
        size_t dep_count = am_map_length(current_ast->alloc, current_ast->dependencies);
        am_value_t *dep_keys = am_map_keys(current_ast->alloc, current_ast->dependencies);

        for (size_t i = 0; i < dep_count; i++) {
            am_value_t path_h_val = am_map_get(current_ast->alloc, current_ast->dependencies, dep_keys[i]);
            if (!am_value_is_handle(path_h_val)) continue;

            am_value_t path_node_val = am_ast_get_node(current_ast, am_value_to_handle(path_h_val));
            if (!am_value_is_ptr(path_node_val)) continue;

            am_object_t *obj = am_value_to_ptr(path_node_val);
            if (obj->type != AM_OBJECT_TYPE_WSTRING) continue;

            wchar_t *dep_path = linker_extract_path_from_wstring((am_wstring_t *)obj);
            if (!dep_path) {
                free(dep_keys);
                return -1;
            }

            wchar_t *abs_dep_path = linker_resolve_path(ctx->base_dir, dep_path);
            free(dep_path);
            if (!abs_dep_path) {
                free(dep_keys);
                return -1;
            }

            int32_t res = import_analysis(ctx, abs_dep_path, current_module_index);
            free(abs_dep_path);
            if (res < 0) {
                free(dep_keys);
                return -1;
            }
        }

        if (dep_keys) free(dep_keys);
    }

    if (importer_index != SIZE_MAX) {
        if (ctx->edge_num >= AM_LINKER_MAX_MODULES * AM_LINKER_MAX_MODULES) return -1;
        ctx->DAG[ctx->edge_num][0] = importer_index;
        ctx->DAG[ctx->edge_num][1] = current_module_index;
        ctx->edge_num++;
    }

    return 0;
}


// ===============================================================================
// 拓扑排序
// ===============================================================================

// 向邻接表中追加一个邻接节点。成功返回0，失败返回-1。
static int32_t topo_sort_push_adj(size_t **adj, size_t *len, size_t *cap, size_t node) {
    if (*len >= *cap) {
        size_t new_cap = *cap ? *cap * 2 : 4;
        size_t *new_adj = (size_t *)realloc(*adj, new_cap * sizeof(size_t));
        if (!new_adj) return -1;
        *adj = new_adj;
        *cap = new_cap;
    }
    (*adj)[(*len)++] = node;
    return 0;
}


// 功能描述：对DAG进行拓扑排序。
// 参数说明：DAG[i] = {出节点索引, 入节点索引}，表示一条从出节点指向入节点的有向边。
//          edge_num 为边的数量。
// 返回值：  成功返回排序后的节点索引数组（由调用者释放），长度等于节点总数（最大索引+1）。
//          失败（如检测到环或内存分配失败）返回 (size_t *)SIZE_MAX。
// 算法：    Kahn算法（基于入度的BFS拓扑排序）。
size_t *am_topo_sort(size_t DAG[][2], size_t edge_num) {
    if (!DAG && edge_num > 0) return (size_t *)SIZE_MAX;

    // 计算节点总数：取所有边中最大索引 + 1
    size_t node_count = 0;
    for (size_t i = 0; i < edge_num; i++) {
        if (DAG[i][0] >= node_count) node_count = DAG[i][0] + 1;
        if (DAG[i][1] >= node_count) node_count = DAG[i][1] + 1;
    }

    size_t *in_degree = (size_t *)calloc(node_count, sizeof(size_t));
    size_t *adj_len = (size_t *)calloc(node_count, sizeof(size_t));
    size_t *adj_cap = (size_t *)calloc(node_count, sizeof(size_t));
    size_t **adj = (size_t **)calloc(node_count, sizeof(size_t *));

    if ((!in_degree || !adj_len || !adj_cap || !adj) && node_count > 0) {
        free(in_degree);
        free(adj_len);
        free(adj_cap);
        free(adj);
        return (size_t *)SIZE_MAX;
    }

    // 构建邻接表和入度数组
    for (size_t i = 0; i < edge_num; i++) {
        size_t out = DAG[i][0];
        size_t in = DAG[i][1];
        in_degree[in]++;
        if (topo_sort_push_adj(&adj[out], &adj_len[out], &adj_cap[out], in) < 0) {
            for (size_t j = 0; j < node_count; j++) free(adj[j]);
            free(in_degree);
            free(adj_len);
            free(adj_cap);
            free(adj);
            return (size_t *)SIZE_MAX;
        }
    }

    size_t *result = (size_t *)malloc(node_count * sizeof(size_t));
    size_t *queue = (size_t *)malloc(node_count * sizeof(size_t));

    if ((!result || !queue) && node_count > 0) {
        free(result);
        free(queue);
        for (size_t j = 0; j < node_count; j++) free(adj[j]);
        free(in_degree);
        free(adj_len);
        free(adj_cap);
        free(adj);
        return (size_t *)SIZE_MAX;
    }

    // Kahn算法：将入度为0的节点入队
    size_t front = 0, rear = 0;
    for (size_t i = 0; i < node_count; i++) {
        if (in_degree[i] == 0) {
            queue[rear++] = i;
        }
    }

    // 依次取出节点，并将其邻接节点入度减1
    size_t result_idx = 0;
    while (front < rear) {
        size_t node = queue[front++];
        result[result_idx++] = node;

        for (size_t i = 0; i < adj_len[node]; i++) {
            size_t neighbor = adj[node][i];
            if (--in_degree[neighbor] == 0) {
                queue[rear++] = neighbor;
            }
        }
    }

    for (size_t j = 0; j < node_count; j++) free(adj[j]);
    free(in_degree);
    free(adj_len);
    free(adj_cap);
    free(adj);
    free(queue);

    // 若结果节点数不足，说明图中存在环
    if (result_idx != node_count) {
        free(result);
        return (size_t *)SIZE_MAX;
    }

    return result;
}


// ===============================================================================
// 链接器入口
// ===============================================================================

// 功能描述：链接器入口。从 main_ast 出发，递归解析所有依赖模块，按拓扑顺序合并成一个大 AST。
// 参数说明：main_ast 为引用根模块的 AST；base_dir 为基准工作目录（用于解析相对路径 import）。
// 返回值：  成功返回链接后的 AST（即基于 main_ast 修改后的 AST）；失败返回 NULL。
am_ast_t *am_link(am_ast_t *main_ast, wchar_t *base_dir) {
    if (!main_ast || !main_ast->alloc || !main_ast->absolute_path) return NULL;

    am_linker_ctx_t *ctx = linker_ctx_create(main_ast->alloc, main_ast, base_dir);
    if (!ctx) return NULL;

    // 递归解析所有依赖模块
    if (import_analysis(ctx, main_ast->absolute_path, SIZE_MAX) != 0) {
        linker_ctx_destroy(ctx);
        return NULL;
    }

    if (ctx->module_counter == 0 || ctx->ALLAST[0] != main_ast) {
        linker_ctx_destroy(ctx);
        return NULL;
    }

    // 只有一个模块时无需拓扑排序与合并
    if (ctx->module_counter == 1) {
        linker_ctx_destroy(ctx);
        return main_ast;
    }

    // 对 DAG 做拓扑排序，同时检查是否成环
    size_t *sorted = am_topo_sort(ctx->DAG, ctx->edge_num);
    if (sorted == (size_t *)SIZE_MAX) {
        linker_ctx_destroy(ctx);
        return NULL;
    }

    // 以排序后的第一个模块为全局 importer，逐个吃掉 importee
    am_ast_t *global_ast = ctx->ALLAST[sorted[0]];
    for (size_t i = 1; i < ctx->module_counter; i++) {
        size_t importee_index = sorted[i];
        if (am_ast_merge(global_ast, ctx->ALLAST[importee_index], 0) != 0) {
            free(sorted);
            linker_ctx_destroy(ctx);
            return NULL;
        }
    }

    free(sorted);
    linker_ctx_destroy(ctx);
    return global_ast;
}
