#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "repl.h"
#include "linker.h"
#include "debug.h"
#include "js2scm.h"

#include "native_System.h"
#include "native_Math.h"
#include "native_String.h"
#include "native_LLM.h"
#include "native_Table.h"

#ifndef AM_ALLOCATOR_POOL_SIZE
#define AM_ALLOCATOR_POOL_SIZE ((size_t)(256ULL * 1024 * 1024))
#endif

// 前向声明
static void repl_ctx_output_wchar(repl_ctx_t *ctx, wchar_t c);
static void repl_ctx_output_wcs(repl_ctx_t *ctx, const wchar_t *s);
static void repl_ctx_clear_output(repl_ctx_t *ctx);
static int repl_ctx_accum_append(repl_ctx_t *ctx, const wchar_t *line);
static void repl_ctx_reset_accum(repl_ctx_t *ctx);
static int repl_ctx_session_append(repl_ctx_t *ctx, const wchar_t *code, size_t len);
static repl_result_t repl_result_from_ctx(repl_ctx_t *ctx);
static int32_t repl_eval_session(repl_ctx_t *ctx, const wchar_t *session_code,
                                  am_module_t **out_mod, am_process_t **out_proc);
static am_module_t *repl_create_initial_module(repl_ctx_t *ctx);
static int repl_ctx_js_indent(const wchar_t *code);
static repl_result_t repl_ctx_eval_js_accum(repl_ctx_t *ctx, int force);
static repl_result_t repl_ctx_submit(repl_ctx_t *ctx);

// 运行时回调：捕获输出到当前上下文的输出缓冲区。
static void on_tick(am_runtime_t *rt) {
    if (!rt || !rt->output_fifo) return;
    repl_ctx_t *ctx = (repl_ctx_t *)am_get_runtime_host_context(rt);
    if (!ctx) return;
    while (rt->output_fifo->length > 0) {
        am_value_t v = am_list_shift(rt->vm_alloc, rt->output_fifo);
        if (am_value_is_wchar(v)) {
            repl_ctx_output_wchar(ctx, (wchar_t)am_value_to_wchar(v));
        }
    }
}

static void on_halt(am_runtime_t *rt) { (void)rt; }

static void on_error(am_runtime_t *rt) {
    if (!rt) return;
    repl_ctx_t *ctx = (repl_ctx_t *)am_get_runtime_host_context(rt);
    if (ctx) ctx->runtime_error = 1;
}

static wchar_t *mb_to_wchar(const char *src) {
    if (!src) return NULL;
    size_t len = mbstowcs(NULL, src, 0);
    if (len == (size_t)-1) return NULL;
    wchar_t *dst = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!dst) return NULL;
    mbstowcs(dst, src, len + 1);
    return dst;
}

static char *wchar_to_mb(const wchar_t *src) {
    if (!src) return NULL;
    size_t len = wcstombs(NULL, src, 0);
    if (len == (size_t)-1) return NULL;
    char *dst = (char *)malloc(len + 1);
    if (!dst) return NULL;
    wcstombs(dst, src, len + 1);
    return dst;
}

// 检查代码是否构成完整的 S-表达式。
// 返回值： 0 完整；1 未完成（需要更多输入）；-1 括号不匹配等明显错误。
// 若 out_indent 非空，则写入建议的续行缩进层级（未闭合的左括号数）。
static int check_expression_complete(const wchar_t *code, int *out_indent) {
    int depth = 0;
    int in_string = 0;
    int escape = 0;
    int line_comment = 0;

    for (size_t i = 0; code[i] != L'\0'; i++) {
        wchar_t c = code[i];

        if (line_comment) {
            if (c == L'\n') line_comment = 0;
            continue;
        }

        if (in_string) {
            if (escape) {
                escape = 0;
                continue;
            }
            if (c == L'\\') {
                escape = 1;
                continue;
            }
            if (c == L'"') {
                in_string = 0;
            }
            continue;
        }

        if (c == L';') {
            line_comment = 1;
            continue;
        }
        if (c == L'"') {
            in_string = 1;
            continue;
        }

        if (c == L'(' || c == L'[' || c == L'{') {
            depth++;
        } else if (c == L')' || c == L']' || c == L'}') {
            depth--;
            if (depth < 0) return -1;
        }
    }

    if (in_string) return 1;
    if (depth > 0) {
        if (out_indent) *out_indent = depth;
        return 1;
    }
    if (out_indent) *out_indent = 0;
    return 0;
}

// 构造 eval 代码的临时绝对路径：base_dir/__repl__.scm
static wchar_t *repl_make_path(am_allocator_t *alloc, const wchar_t *base_dir) {
    if (!alloc) return NULL;
    const wchar_t *filename = L"__repl__.scm";
    size_t base_len = base_dir ? wcslen(base_dir) : 0;
    size_t file_len = wcslen(filename);
    int need_sep = (base_len > 0 && base_dir[base_len - 1] != L'/' && base_dir[base_len - 1] != L'\\');
    wchar_t *path = (wchar_t *)am_malloc(alloc,
                                          (base_len + (need_sep ? 1 : 0) + file_len + 1) * sizeof(wchar_t));
    if (!path) return NULL;
    if (base_len > 0) wcscpy(path, base_dir);
    if (need_sep) path[base_len] = L'/';
    wcscpy(path + base_len + (need_sep ? 1 : 0), filename);
    return path;
}


// 判断 varid 名字是否对应有副作用的内置函数或本地宿主函数。
static int repl_is_side_effect_varid(const wchar_t *word) {
    if (!word) return 0;

    // 有副作用的全局 builtin（对应 runtime 中的 OP）
    if (wcscmp(word, L"display") == 0) return 1;
    if (wcscmp(word, L"newline") == 0) return 1;
    if (wcscmp(word, L"write") == 0) return 1;
    if (wcscmp(word, L"read") == 0) return 1;
    if (wcscmp(word, L"set_item!") == 0) return 1;
    if (wcscmp(word, L"push") == 0) return 1;
    if (wcscmp(word, L"pop") == 0) return 1;
    if (wcscmp(word, L"fork") == 0) return 1;

    // 有副作用的本地宿主函数（按库分类）
    if (wcscmp(word, L"System.exit") == 0) return 1;
    if (wcscmp(word, L"System.kill") == 0) return 1;
    if (wcscmp(word, L"System.exec") == 0) return 1;
    if (wcscmp(word, L"System.set_timeout") == 0) return 1;
    if (wcscmp(word, L"System.set_interval") == 0) return 1;
    if (wcscmp(word, L"System.clear_timeout") == 0) return 1;
    if (wcscmp(word, L"System.clear_interval") == 0) return 1;
    if (wcscmp(word, L"System.fork") == 0) return 1;
    if (wcscmp(word, L"System.eval") == 0) return 1;
    if (wcscmp(word, L"System.make_queue") == 0) return 1;
    if (wcscmp(word, L"System.write") == 0) return 1;
    if (wcscmp(word, L"System.read") == 0) return 1;
    if (wcscmp(word, L"Table.set") == 0) return 1;
    if (wcscmp(word, L"Table.delete") == 0) return 1;
    if (wcscmp(word, L"LLM.init") == 0) return 1;

    return 0;
}

// 判断节点是否是 define / set! / native / import 调用，或是有副作用的 builtin / native 调用。
// 这些表达式不再额外包装 display。
static int repl_last_expr_has_side_effect(am_ast_t *ast, am_handle_t node_handle) {
    if (!ast || node_handle == AM_HANDLE_NULL) return 0;
    am_value_t node_val = am_ast_get_node(ast, node_handle);
    if (!am_value_is_ptr(node_val)) return 0;
    am_object_t *obj = am_value_to_ptr(node_val);
    if (obj->type != AM_OBJECT_TYPE_LIST) return 0;
    am_list_t *lst = (am_list_t *)obj;
    if (lst->length == 0) return 0;
    am_value_t first = am_list_get(ast->alloc, lst, 0);

    if (am_value_is_symbol(first)) {
        if (first == AM_VALUE_KW_define ||
            first == AM_VALUE_KW_set ||
            first == AM_VALUE_KW_native ||
            first == AM_VALUE_KW_import) {
            return 1;
        }
        // (begin ...) 的副作用取决于最后一个子表达式
        if (first == AM_VALUE_KW_begin && lst->length > 1) {
            am_value_t last = am_list_get(ast->alloc, lst, lst->length - 1);
            if (am_value_is_handle(last)) {
                return repl_last_expr_has_side_effect(ast, am_value_to_handle(last));
            }
        }
    }
    if (am_value_is_varid(first)) {
        am_varid_t varid = am_value_to_varid(first);
        wchar_t *word = am_vocab_get(ast->alloc, ast->var_vocab, &varid);
        if (word) {
            return repl_is_side_effect_varid(word);
        }
    }
    return 0;
}

// 若用户输入的最后一个顶层表达式不是定义/赋值/输出，则将其包装为 (display <expr>) (newline)。
// 返回新分配的 wchar_t* 代码（调用者负责释放），失败返回 NULL。
// 关键：直接从原始 session_code 切分最后一个表达式，避免 ARN 后的内部变量名污染。
static wchar_t *repl_build_session_code(repl_ctx_t *ctx, const wchar_t *session_code) {
    if (!ctx || !session_code) return NULL;
    am_allocator_t *alloc = ctx->vm_alloc;

    const wchar_t *prefix = L"((lambda () \n";
    const wchar_t *suffix = L"\n))";
    size_t prefix_len = wcslen(prefix);
    size_t suffix_len = wcslen(suffix);
    size_t session_len = wcslen(session_code);
    size_t wrapped_len = prefix_len + session_len + suffix_len;

    wchar_t *wrapped_code = (wchar_t *)am_malloc(alloc, (wrapped_len + 1) * sizeof(wchar_t));
    if (!wrapped_code) return NULL;
    wcscpy(wrapped_code, prefix);
    wcscat(wrapped_code, session_code);
    wcscat(wrapped_code, suffix);

    wchar_t *path_buf = repl_make_path(alloc, L".");
    am_ast_t *ast = am_parse(alloc, wrapped_code, path_buf, 0);
    am_free(alloc, path_buf);
    if (!ast) {
        am_free(alloc, wrapped_code);
        return NULL;
    }

    am_value_t *bodies = am_ast_get_global_nodes(ast);
    size_t n_body = 0;
    if (bodies) {
        am_value_t lambda_val = am_ast_get_node(ast, ast->top_lambda_handle);
        if (am_value_is_ptr(lambda_val)) {
            am_list_t *lambda = (am_list_t *)am_value_to_ptr(lambda_val);
            n_body = am_list_lambda_get_body_number(ast->alloc, lambda);
        }
    }

    if (n_body == 0 || repl_last_expr_has_side_effect(ast, am_value_to_handle(bodies[n_body - 1]))) {
        if (bodies) free(bodies);
        am_ast_destroy(ast);
        return wrapped_code; // 无需包装，调用者负责释放
    }

    am_handle_t last_handle = am_value_to_handle(bodies[n_body - 1]);
    size_t last_token_idx = am_ast_get_node_token_index(ast, last_handle);
    size_t char_index = 0;
    if (last_token_idx != SIZE_MAX && last_token_idx < ast->token_count) {
        char_index = ast->tokens[last_token_idx].index;
    }

    if (bodies) free(bodies);
    am_ast_destroy(ast);

    // char_index 是最后一个表达式在 wrapped_code 中的起始字符偏移。
    // 转换到 session_code 中的起始偏移。
    size_t session_start = 0;
    if (last_token_idx != SIZE_MAX && char_index >= prefix_len) {
        session_start = char_index - prefix_len;
    } else {
        // 对于没有 token 映射的原子表达式（如单独的数字/字符串），回退到整个 session_code
        char_index = prefix_len;
    }
    // 最后一个表达式在 session_code 中的结束偏移，忽略尾部空白。
    size_t session_end = session_len;
    while (session_end > session_start &&
           (session_code[session_end - 1] == L'\n' ||
            session_code[session_end - 1] == L'\r' ||
            session_code[session_end - 1] == L' ' ||
            session_code[session_end - 1] == L'\t')) {
        session_end--;
    }
    size_t last_expr_len = (session_end > session_start) ? (session_end - session_start) : 0;

    size_t new_len = char_index + wcslen(L"\n(display ") + last_expr_len + wcslen(L")\n(newline)") + suffix_len + 8;
    wchar_t *new_code = (wchar_t *)am_malloc(alloc, (new_len + 1) * sizeof(wchar_t));
    if (!new_code) {
        am_free(alloc, wrapped_code);
        return NULL;
    }

    wcsncpy(new_code, wrapped_code, char_index);
    new_code[char_index] = L'\0';
    wcscat(new_code, L"\n(display ");
    wcsncat(new_code, session_code + session_start, last_expr_len);
    wcscat(new_code, L")\n(newline)");
    wcscat(new_code, suffix);

    am_free(alloc, wrapped_code);
    return new_code;
}

// 编译并执行完整的 REPL 会话代码。成功返回 0，失败返回 -1。
static int32_t repl_eval_session(repl_ctx_t *ctx, const wchar_t *session_code,
                                  am_module_t **out_mod, am_process_t **out_proc) {
    if (!ctx || !session_code || !out_mod || !out_proc) return -1;

    wchar_t *display_wrapped = repl_build_session_code(ctx, session_code);
    if (!display_wrapped) {
        repl_ctx_output_wcs(ctx, L"[REPL] 语法解析失败\n");
        return -1;
    }

    wchar_t *path_buf = repl_make_path(ctx->vm_alloc, L".");
    if (!path_buf) {
        am_free(ctx->vm_alloc, display_wrapped);
        return -1;
    }

    am_ast_t *ast = am_parse(ctx->vm_alloc, display_wrapped, path_buf, 0);
    am_free(ctx->vm_alloc, path_buf);
    am_free(ctx->vm_alloc, display_wrapped);
    if (!ast) {
        repl_ctx_output_wcs(ctx, L"[REPL] 语法解析失败\n");
        return -1;
    }

    am_module_t *mod = am_compile(ast, 0, 0);
    if (!mod) {
        am_ast_destroy(ast);
        repl_ctx_output_wcs(ctx, L"[REPL] 编译失败\n");
        return -1;
    }

    am_process_t *proc = am_process_load_from_module(ctx->vm_alloc, ctx->heap_alloc, mod);
    if (!proc) {
        if (mod->ilcode) am_free(ctx->vm_alloc, mod->ilcode);
        if (mod->ast) am_ast_destroy(mod->ast);
        am_free(ctx->vm_alloc, mod);
        repl_ctx_output_wcs(ctx, L"[REPL] 加载进程失败\n");
        return -1;
    }

    *out_mod = mod;
    *out_proc = proc;

    return 0;
}


static am_module_t *repl_create_initial_module(repl_ctx_t *ctx) {
    if (!ctx) return NULL;
    const wchar_t *init_code = L"((lambda () (begin)))";
    wchar_t *path_buf = repl_make_path(ctx->vm_alloc, L".");
    if (!path_buf) return NULL;

    am_ast_t *ast = am_parse(ctx->vm_alloc, (wchar_t *)init_code, path_buf, 0);
    am_free(ctx->vm_alloc, path_buf);
    if (!ast) return NULL;

    am_module_t *mod = am_compile(ast, 0, 0);
    if (!mod) {
        am_ast_destroy(ast);
        return NULL;
    }

    return mod;
}

// 输出缓冲区操作
static void repl_ctx_clear_output(repl_ctx_t *ctx) {
    if (!ctx) return;
    ctx->output_len = 0;
    if (ctx->output_buf) ctx->output_buf[0] = '\0';
    ctx->output = NULL;
}

static void repl_ctx_output_mb(repl_ctx_t *ctx, const char *s) {
    if (!ctx || !s) return;
    size_t len = strlen(s);
    size_t need = ctx->output_len + len + 1;
    if (need > ctx->output_cap) {
        size_t new_cap = ctx->output_cap ? ctx->output_cap * 2 : 256;
        while (new_cap < need) new_cap *= 2;
        char *new_buf = (char *)realloc(ctx->output_buf, new_cap);
        if (!new_buf) return;
        ctx->output_buf = new_buf;
        ctx->output_cap = new_cap;
    }
    memcpy(ctx->output_buf + ctx->output_len, s, len);
    ctx->output_len += len;
    ctx->output_buf[ctx->output_len] = '\0';
    ctx->output = ctx->output_buf;
}

static void repl_ctx_output_wchar(repl_ctx_t *ctx, wchar_t c) {
    char mb[MB_CUR_MAX + 1];
    int n = wctomb(mb, c);
    if (n <= 0) return;
    mb[n] = '\0';
    repl_ctx_output_mb(ctx, mb);
}

static void repl_ctx_output_wcs(repl_ctx_t *ctx, const wchar_t *s) {
    if (!ctx || !s) return;
    char *mb = wchar_to_mb(s);
    if (!mb) return;
    repl_ctx_output_mb(ctx, mb);
    free(mb);
}

// 累积输入与会话操作
static int repl_ctx_accum_append(repl_ctx_t *ctx, const wchar_t *line) {
    if (!ctx || !line) return 0;
    size_t line_len = wcslen(line);
    size_t need = ctx->accum_len + (ctx->accum_len > 0 ? 1 : 0) + line_len;
    wchar_t *new_accum = (wchar_t *)realloc(ctx->accum, (need + 1) * sizeof(wchar_t));
    if (!new_accum) return 0;
    ctx->accum = new_accum;
    if (ctx->accum_len > 0) {
        ctx->accum[ctx->accum_len] = L'\n';
        ctx->accum_len++;
    }
    memcpy(ctx->accum + ctx->accum_len, line, line_len * sizeof(wchar_t));
    ctx->accum_len += line_len;
    ctx->accum[ctx->accum_len] = L'\0';
    return 1;
}

static void repl_ctx_reset_accum(repl_ctx_t *ctx) {
    if (!ctx) return;
    free(ctx->accum);
    ctx->accum = NULL;
    ctx->accum_len = 0;
    ctx->multiline = 0;
}

static int repl_ctx_session_append(repl_ctx_t *ctx, const wchar_t *code, size_t len) {
    if (!ctx || !code) return 0;
    size_t need = ctx->session_len + (ctx->session_len > 0 ? 1 : 0) + len;
    wchar_t *new_session = (wchar_t *)realloc(ctx->session, (need + 1) * sizeof(wchar_t));
    if (!new_session) return 0;
    ctx->session = new_session;
    if (ctx->session_len > 0) {
        ctx->session[ctx->session_len] = L'\n';
        ctx->session_len++;
    }
    memcpy(ctx->session + ctx->session_len, code, len * sizeof(wchar_t));
    ctx->session_len += len;
    ctx->session[ctx->session_len] = L'\0';
    return 1;
}

static void repl_ctx_rollback_session(repl_ctx_t *ctx, size_t submitted_len) {
    if (!ctx) return;
    if (ctx->session_len > submitted_len) {
        ctx->session_len -= submitted_len + 1;
    } else {
        ctx->session_len = 0;
    }
    if (ctx->session) ctx->session[ctx->session_len] = L'\0';
}

static repl_result_t repl_result_from_ctx(repl_ctx_t *ctx) {
    repl_result_t res;
    res.status = ctx ? ctx->status : REPL_STATUS_ERROR;
    res.output = ctx ? ctx->output : NULL;
    res.indent = ctx ? ctx->indent : 0;
    return res;
}

// 去掉 JS 翻译器自动添加的最外层 ((lambda () ...)) 包装，
// 使后续的 repl_build_session_code 能重新包装并自动 display 最后一个表达式。
// 返回指向 scheme 缓冲区内部的指针；scheme 仍由调用者负责 free。
static wchar_t *repl_ctx_unwrap_js_scheme(wchar_t *scheme) {
    if (!scheme) return NULL;
    size_t len = wcslen(scheme);
    const wchar_t *prefix = L"((lambda ()";
    size_t prefix_len = wcslen(prefix);
    if (len < prefix_len || wcsncmp(scheme, prefix, prefix_len) != 0) {
        return scheme;
    }

    // 去掉尾部空白，找到最后的 "))"
    while (len > 0 &&
           (scheme[len - 1] == L'\n' || scheme[len - 1] == L'\r' ||
            scheme[len - 1] == L' ' || scheme[len - 1] == L'\t')) {
        scheme[len - 1] = L'\0';
        len--;
    }

    if (len >= 2 && scheme[len - 1] == L')' && scheme[len - 2] == L')') {
        scheme[len - 2] = L'\0';
        return scheme + prefix_len;
    }

    return scheme;
}

// 计算 JS 代码的建议缩进层级（未闭合的括号/花括号/方括号数）。
static int repl_ctx_js_indent(const wchar_t *code) {
    if (!code) return 0;
    int depth = 0;
    int in_string = 0;
    int escape = 0;
    int line_comment = 0;
    int block_comment = 0;

    for (size_t i = 0; code[i] != L'\0'; i++) {
        wchar_t c = code[i];
        wchar_t next = code[i + 1];

        if (line_comment) {
            if (c == L'\n') line_comment = 0;
            continue;
        }
        if (block_comment) {
            if (c == L'*' && next == L'/') {
                block_comment = 0;
                i++;
            }
            continue;
        }

        if (in_string) {
            if (escape) {
                escape = 0;
                continue;
            }
            if (c == L'\\') {
                escape = 1;
                continue;
            }
            if (c == L'"' || c == L'\'') {
                in_string = 0;
            }
            continue;
        }

        if (c == L'/' && next == L'/') {
            line_comment = 1;
            i++;
            continue;
        }
        if (c == L'/' && next == L'*') {
            block_comment = 1;
            i++;
            continue;
        }
        if (c == L'"' || c == L'\'') {
            in_string = 1;
            continue;
        }

        if (c == L'(' || c == L'[' || c == L'{') {
            depth++;
        } else if (c == L')' || c == L']' || c == L'}') {
            depth--;
        }
    }

    if (depth < 0) depth = 0;
    return depth;
}

// 将当前 accum 中的 JS 代码翻译成 Scheme 并执行。
// force=0 时翻译失败仅表示代码尚不完整，返回 CONTINUE；
// force=1 时（空行触发）翻译失败则报错误并清空 accum。
static repl_result_t repl_ctx_eval_js_accum(repl_ctx_t *ctx, int force) {
    if (!ctx) return repl_result_from_ctx(ctx);

    // 非强制提交时，若 JS 代码括号/花括号明显未闭合，暂不翻译，避免打印错误信息。
    int js_indent = repl_ctx_js_indent(ctx->accum);
    if (!force && js_indent > 0) {
        ctx->status = REPL_STATUS_CONTINUE;
        ctx->indent = js_indent;
        return repl_result_from_ctx(ctx);
    }

    wchar_t *scheme = am_js_to_scheme(ctx->accum);
    if (!scheme) {
        if (force) {
            repl_ctx_reset_accum(ctx);
            ctx->status = REPL_STATUS_ERROR;
            repl_ctx_output_wcs(ctx, L"[REPL] JS 翻译失败\n");
        } else {
            ctx->status = REPL_STATUS_CONTINUE;
            ctx->indent = js_indent;
        }
        return repl_result_from_ctx(ctx);
    }

    // 翻译成功：去掉 JS 翻译器自带的外层 lambda 包装，
    // 让 repl_build_session_code 重新包装并自动 display 最后一个表达式。
    wchar_t *unwrapped = repl_ctx_unwrap_js_scheme(scheme);
    size_t unwrapped_len = wcslen(unwrapped);
    wchar_t *scheme_copy = (wchar_t *)malloc((unwrapped_len + 1) * sizeof(wchar_t));
    if (!scheme_copy) {
        free(scheme);
        repl_ctx_reset_accum(ctx);
        ctx->status = REPL_STATUS_ERROR;
        repl_ctx_output_wcs(ctx, L"[REPL] 内存不足\n");
        return repl_result_from_ctx(ctx);
    }
    wcscpy(scheme_copy, unwrapped);
    free(scheme);

    wchar_t *js_accum = ctx->accum;
    ctx->accum = scheme_copy;
    ctx->accum_len = unwrapped_len;
    ctx->multiline = 0;

    repl_result_t res = repl_ctx_submit(ctx);

    // submit 已释放 scheme_copy 并清空 accum；仍需释放原始 JS 累积缓冲区。
    free(js_accum);
    return res;
}

// 提交当前 accum 到 session 并执行。
static repl_result_t repl_ctx_submit(repl_ctx_t *ctx) {
    if (!ctx) return repl_result_from_ctx(ctx);

    size_t submitted_len = ctx->accum_len;
    if (!repl_ctx_session_append(ctx, ctx->accum, ctx->accum_len)) {
        repl_ctx_reset_accum(ctx);
        ctx->status = REPL_STATUS_ERROR;
        repl_ctx_output_wcs(ctx, L"[REPL] 内存不足\n");
        return repl_result_from_ctx(ctx);
    }
    repl_ctx_reset_accum(ctx);

    am_module_t *new_mod = NULL;
    am_process_t *new_proc = NULL;
    if (repl_eval_session(ctx, ctx->session, &new_mod, &new_proc) != 0) {
        repl_ctx_rollback_session(ctx, submitted_len);
        ctx->status = REPL_STATUS_ERROR;
        return repl_result_from_ctx(ctx);
    }

    ctx->runtime_error = 0;
    am_pid_t new_pid = am_runtime_load_module(ctx->rt, new_mod);

    if (new_pid == (am_pid_t)-1) {
        if (new_mod->ilcode) {
            am_free(ctx->vm_alloc, new_mod->ilcode);
            new_mod->ilcode = NULL;
        }
        if (new_mod->ast) {
            am_ast_destroy(new_mod->ast);
            new_mod->ast = NULL;
        }
        am_free(ctx->vm_alloc, new_mod);
        repl_ctx_rollback_session(ctx, submitted_len);
        ctx->status = REPL_STATUS_ERROR;
        repl_ctx_output_wcs(ctx, L"[REPL] 加载新进程失败\n");
        return repl_result_from_ctx(ctx);
    }

    if (new_mod->ilcode) {
        am_free(ctx->vm_alloc, new_mod->ilcode);
        new_mod->ilcode = NULL;
    }
    if (new_mod->ast) {
        am_ast_destroy(new_mod->ast);
        new_mod->ast = NULL;
    }
    am_free(ctx->vm_alloc, new_mod);

    int32_t running = 1;
    while (running && !ctx->should_stop) {
        running = am_runtime_tick(ctx->rt, ctx->rt->timeslice);
    }

    // 确保所有输出都被捕获
    on_tick(ctx->rt);

    if (ctx->runtime_error) {
        am_runtime_kill_process(ctx->rt, new_pid);
        repl_ctx_rollback_session(ctx, submitted_len);
        ctx->status = REPL_STATUS_ERROR;
        return repl_result_from_ctx(ctx);
    }

    am_runtime_kill_process(ctx->rt, ctx->pid);
    ctx->pid = new_pid;
    ctx->status = REPL_STATUS_OUTPUT;
    return repl_result_from_ctx(ctx);
}

// 建立 REPL 上下文。
repl_ctx_t *repl_ctx_create(void) {
    repl_ctx_t *ctx = (repl_ctx_t *)calloc(1, sizeof(repl_ctx_t));
    if (!ctx) return NULL;

    ctx->pid = (am_pid_t)-1;

    ctx->pool = am_allocator_pool_create(AM_ALLOCATOR_POOL_SIZE);
    if (!ctx->pool) {
        free(ctx);
        return NULL;
    }
    ctx->vm_alloc = am_allocator_pool_get_vm(ctx->pool);
    ctx->heap_alloc = am_allocator_pool_get_heap(ctx->pool);

    wchar_t cwd_w[2] = { L'.', L'\0' };
    ctx->rt = am_runtime_create(ctx->vm_alloc, ctx->heap_alloc, cwd_w);
    if (!ctx->rt) {
        am_allocator_pool_destroy(ctx->pool);
        free(ctx);
        return NULL;
    }

    am_runtime_set_default_timeslice(ctx->rt, 8192);
    am_set_runtime_host_context(ctx->rt, ctx);

    am_runtime_register_native_lib(ctx->rt, &am_native_System_lib);
    am_runtime_register_native_lib(ctx->rt, &am_native_Math_lib);
    am_runtime_register_native_lib(ctx->rt, &am_native_String_lib);
    am_runtime_register_native_lib(ctx->rt, &am_native_LLM_lib);
    am_runtime_register_native_lib(ctx->rt, &am_native_Table_lib);

    ctx->rt->callback_on_halt = on_halt;
    ctx->rt->callback_on_error = on_error;
    ctx->rt->callback_on_tick = on_tick;

    am_module_t *mod = repl_create_initial_module(ctx);
    if (!mod) {
        am_runtime_destroy(ctx->rt);
        am_allocator_pool_destroy(ctx->pool);
        free(ctx);
        return NULL;
    }

    ctx->pid = am_runtime_load_module(ctx->rt, mod);
    if (ctx->pid == (am_pid_t)-1) {
        if (mod->ilcode) am_free(ctx->vm_alloc, mod->ilcode);
        if (mod->ast) am_ast_destroy(mod->ast);
        am_free(ctx->vm_alloc, mod);
        am_runtime_destroy(ctx->rt);
        am_allocator_pool_destroy(ctx->pool);
        free(ctx);
        return NULL;
    }

    if (mod->ilcode) {
        am_free(ctx->vm_alloc, mod->ilcode);
        mod->ilcode = NULL;
    }
    if (mod->ast) {
        am_ast_destroy(mod->ast);
        mod->ast = NULL;
    }
    am_free(ctx->vm_alloc, mod);

    ctx->status = REPL_STATUS_CONTINUE;
    ctx->output = NULL;
    ctx->indent = 0;
    ctx->js_mode = 0;
    ctx->should_stop = 0;
    ctx->prompt_main = "animac> ";
    ctx->prompt_cont = "... ";

    return ctx;
}

// 请求停止 REPL 运行。
void repl_ctx_interrupt(repl_ctx_t *ctx) {
    if (ctx) ctx->should_stop = 1;
}

// 设置 JS 解释器模式。
void repl_ctx_set_js_mode(repl_ctx_t *ctx, int js_mode) {
    if (!ctx) return;
    ctx->js_mode = js_mode ? 1 : 0;
    if (ctx->js_mode) {
        ctx->prompt_main = "js> ";
        ctx->prompt_cont = "js ... ";
    } else {
        ctx->prompt_main = "animac> ";
        ctx->prompt_cont = "... ";
    }
}

// 销毁 REPL 上下文。
void repl_ctx_destroy(repl_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->rt) {
        if (ctx->pid != (am_pid_t)-1) {
            am_runtime_kill_process(ctx->rt, ctx->pid);
        }
        am_runtime_destroy(ctx->rt);
    }
    if (ctx->pool) am_allocator_pool_destroy(ctx->pool);
    free(ctx->accum);
    free(ctx->session);
    free(ctx->output_buf);
    free(ctx);
}

// Scheme 模式：逐行累积并检查 S-表达式完整性。
static repl_result_t repl_ctx_feed_scheme(repl_ctx_t *ctx, const char *input) {
    if (!ctx) return repl_result_from_ctx(ctx);

    // 空行：尝试提交已累积的输入
    if (!input || input[0] == '\0') {
        if (!ctx->accum || ctx->accum_len == 0) {
            ctx->status = REPL_STATUS_CONTINUE;
            return repl_result_from_ctx(ctx);
        }
        int status = check_expression_complete(ctx->accum, &ctx->indent);
        if (status == 1) {
            ctx->status = REPL_STATUS_CONTINUE;
            return repl_result_from_ctx(ctx);
        }
        if (status == -1) {
            repl_ctx_reset_accum(ctx);
            ctx->status = REPL_STATUS_ERROR;
            repl_ctx_output_wcs(ctx, L"[REPL] 括号不匹配\n");
            return repl_result_from_ctx(ctx);
        }
        // status == 0，fall through 到提交
    } else {
        wchar_t *line_w = mb_to_wchar(input);
        if (!line_w) {
            ctx->status = REPL_STATUS_ERROR;
            repl_ctx_output_wcs(ctx, L"[REPL] 输入编码失败\n");
            return repl_result_from_ctx(ctx);
        }

        // 退出命令（仅在尚未进入多行输入时生效）
        if (!ctx->multiline && (!ctx->accum || ctx->accum_len == 0)) {
            if (wcscmp(line_w, L"exit") == 0 ||
                wcscmp(line_w, L"quit") == 0 ||
                wcscmp(line_w, L"(exit)") == 0 ||
                wcscmp(line_w, L"(System.exit)") == 0) {
                free(line_w);
                ctx->status = REPL_STATUS_EXIT;
                return repl_result_from_ctx(ctx);
            }
        }

        if (!repl_ctx_accum_append(ctx, line_w)) {
            free(line_w);
            ctx->status = REPL_STATUS_ERROR;
            repl_ctx_output_wcs(ctx, L"[REPL] 内存不足\n");
            return repl_result_from_ctx(ctx);
        }
        free(line_w);

        int status = check_expression_complete(ctx->accum, &ctx->indent);
        if (status == 1) {
            ctx->multiline = 1;
            ctx->status = REPL_STATUS_CONTINUE;
            return repl_result_from_ctx(ctx);
        }
        if (status == -1) {
            repl_ctx_reset_accum(ctx);
            ctx->status = REPL_STATUS_ERROR;
            repl_ctx_output_wcs(ctx, L"[REPL] 括号不匹配\n");
            return repl_result_from_ctx(ctx);
        }
        // status == 0，fall through 到提交
    }

    return repl_ctx_submit(ctx);
}

// JS 模式：逐行累积 JS，每次尝试翻译成 Scheme；翻译成功则执行。
static repl_result_t repl_ctx_feed_js(repl_ctx_t *ctx, const char *input) {
    if (!ctx) return repl_result_from_ctx(ctx);

    // 空行：强制尝试翻译一次
    if (!input || input[0] == '\0') {
        if (!ctx->accum || ctx->accum_len == 0) {
            ctx->status = REPL_STATUS_CONTINUE;
            return repl_result_from_ctx(ctx);
        }
        return repl_ctx_eval_js_accum(ctx, 1);
    }

    wchar_t *line_w = mb_to_wchar(input);
    if (!line_w) {
        ctx->status = REPL_STATUS_ERROR;
        repl_ctx_output_wcs(ctx, L"[REPL] 输入编码失败\n");
        return repl_result_from_ctx(ctx);
    }

    // 退出命令（仅在尚未进入多行输入时生效）
    if (!ctx->multiline && (!ctx->accum || ctx->accum_len == 0)) {
        if (wcscmp(line_w, L"exit") == 0 ||
            wcscmp(line_w, L"quit") == 0 ||
            wcscmp(line_w, L"(exit)") == 0 ||
            wcscmp(line_w, L"(System.exit)") == 0) {
            free(line_w);
            ctx->status = REPL_STATUS_EXIT;
            return repl_result_from_ctx(ctx);
        }
    }

    if (!repl_ctx_accum_append(ctx, line_w)) {
        free(line_w);
        ctx->status = REPL_STATUS_ERROR;
        repl_ctx_output_wcs(ctx, L"[REPL] 内存不足\n");
        return repl_result_from_ctx(ctx);
    }
    free(line_w);

    // 每次追加后都尝试翻译；若失败则认为 JS 仍不完整，继续输入。
    return repl_ctx_eval_js_accum(ctx, 0);
}

// 向 REPL 上下文传入一个字符串（通常是一行输入），返回结果并更新上下文内部状态。
repl_result_t repl_ctx_feed(repl_ctx_t *ctx, const char *input) {
    if (!ctx) {
        repl_result_t res;
        res.status = REPL_STATUS_ERROR;
        res.output = NULL;
        res.indent = 0;
        return res;
    }

    repl_ctx_clear_output(ctx);
    ctx->status = REPL_STATUS_CONTINUE;
    ctx->indent = 0;
    ctx->output = NULL;
    ctx->should_stop = 0;

    if (ctx->js_mode) {
        return repl_ctx_feed_js(ctx, input);
    }
    return repl_ctx_feed_scheme(ctx, input);
}


