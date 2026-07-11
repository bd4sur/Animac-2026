#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <signal.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "utils.h"
#include "parser.h"
#include "linker.h"
#include "compiler.h"
#include "module.h"
#include "process.h"
#include "runtime.h"
#include "debug.h"
#include "ast.h"
#include "list.h"
#include "vocab.h"
#include "heap.h"
#include "object.h"

#include "native_System.h"
#include "native_Math.h"
#include "native_String.h"
#include "native_LLM.h"
#include "native_Table.h"

#ifndef AM_ALLOCATOR_POOL_SIZE
#define AM_ALLOCATOR_POOL_SIZE ((size_t)(256ULL * 1024 * 1024))
#endif

static am_allocator_pool_t *g_pool = NULL;
static am_allocator_t *g_vm_alloc = NULL;
static am_allocator_t *g_heap_alloc = NULL;
static am_runtime_t *g_rt = NULL;
static volatile int g_should_stop = 0;
static volatile int g_runtime_error = 0;

static void on_tick(am_runtime_t *rt) {
    if (!rt || !rt->output_fifo) return;
    while (rt->output_fifo->length > 0) {
        am_value_t v = am_list_shift(rt->vm_alloc, rt->output_fifo);
        if (am_value_is_wchar(v)) {
            printf("%lc", (wchar_t)am_value_to_wchar(v));
        }
    }
    fflush(stdout);
}

static void on_halt(am_runtime_t *rt) { (void)rt; }
static void on_error(am_runtime_t *rt) { (void)rt; g_runtime_error = 1; }

static void signal_handler(int sig) {
    (void)sig;
    g_should_stop = 1;
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


// 判断节点是否是 define / set! / display / newline 调用，这些不再额外包装 display。
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
        if (first == AM_VALUE_KW_define || first == AM_VALUE_KW_set) return 1;
    }
    if (am_value_is_varid(first)) {
        am_varid_t varid = am_value_to_varid(first);
        wchar_t *word = am_vocab_get(ast->alloc, ast->var_vocab, &varid);
        if (word) {
            if (wcscmp(word, L"display") == 0) return 1;
            if (wcscmp(word, L"newline") == 0) return 1;
        }
    }
    return 0;
}

// 若用户输入的最后一个顶层表达式不是定义/赋值/输出，则将其包装为 (display <expr>) (newline)。
// 返回新分配的 wchar_t* 代码（调用者负责释放），失败返回 NULL。
// 关键：直接从原始 session_code 切分最后一个表达式，避免 ARN 后的内部变量名污染。
static wchar_t *repl_build_session_code(am_allocator_t *alloc, const wchar_t *session_code) {
    if (!alloc || !session_code) return NULL;

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
static int32_t repl_eval_session(const wchar_t *session_code, am_module_t **out_mod, am_process_t **out_proc) {
    if (!g_rt || !session_code || !out_mod || !out_proc) return -1;

    wchar_t *display_wrapped = repl_build_session_code(g_vm_alloc, session_code);
    if (!display_wrapped) return -1;

    wchar_t *path_buf = repl_make_path(g_vm_alloc, L".");
    if (!path_buf) {
        am_free(g_vm_alloc, display_wrapped);
        return -1;
    }

    am_ast_t *ast = am_parse(g_vm_alloc, display_wrapped, path_buf, 0);
    am_free(g_vm_alloc, path_buf);
    am_free(g_vm_alloc, display_wrapped);
    if (!ast) {
        fwprintf(stderr, L"[REPL] 语法解析失败\n");
        return -1;
    }

    am_module_t *mod = am_compile(ast, 0, 0);
    if (!mod) {
        am_ast_destroy(ast);
        fwprintf(stderr, L"[REPL] 编译失败\n");
        return -1;
    }

    am_process_t *proc = am_process_load_from_module(g_vm_alloc, g_heap_alloc, mod);
    if (!proc) {
        if (mod->ilcode) am_free(g_vm_alloc, mod->ilcode);
        if (mod->ast) am_ast_destroy(mod->ast);
        am_free(g_vm_alloc, mod);
        fwprintf(stderr, L"[REPL] 加载进程失败\n");
        return -1;
    }

    *out_mod = mod;
    *out_proc = proc;

    return 0;
}


static am_module_t *repl_create_initial_module(void) {
    const wchar_t *init_code = L"((lambda () (begin)))";
    wchar_t *path_buf = repl_make_path(g_vm_alloc, L".");
    if (!path_buf) return NULL;

    am_ast_t *ast = am_parse(g_vm_alloc, (wchar_t *)init_code, path_buf, 0);
    am_free(g_vm_alloc, path_buf);
    if (!ast) return NULL;

    am_module_t *mod = am_compile(ast, 0, 0);
    if (!mod) {
        am_ast_destroy(ast);
        return NULL;
    }

    return mod;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (!setlocale(LC_ALL, "")) {
        fprintf(stderr, "setlocale failed\n");
        return 1;
    }

    signal(SIGINT, signal_handler);

    g_pool = am_allocator_pool_create(AM_ALLOCATOR_POOL_SIZE);
    if (!g_pool) {
        fprintf(stderr, "Failed to create allocator pool\n");
        return 1;
    }
    g_vm_alloc = am_allocator_pool_get_vm(g_pool);
    g_heap_alloc = am_allocator_pool_get_heap(g_pool);

    wchar_t cwd_w[2] = { L'.', L'\0' };
    g_rt = am_runtime_create(g_vm_alloc, g_heap_alloc, cwd_w);
    if (!g_rt) {
        fprintf(stderr, "Failed to create runtime\n");
        am_allocator_pool_destroy(g_pool);
        return 1;
    }

    am_runtime_set_default_timeslice(g_rt, 8192);
    am_set_runtime_host_context(g_rt, NULL);

    am_runtime_register_native_lib(g_rt, &am_native_System_lib);
    am_runtime_register_native_lib(g_rt, &am_native_Math_lib);
    am_runtime_register_native_lib(g_rt, &am_native_String_lib);
    am_runtime_register_native_lib(g_rt, &am_native_LLM_lib);
    am_runtime_register_native_lib(g_rt, &am_native_Table_lib);

    g_rt->callback_on_halt = on_halt;
    g_rt->callback_on_error = on_error;
    g_rt->callback_on_tick = on_tick;

    am_module_t *mod = repl_create_initial_module();
    if (!mod) {
        fprintf(stderr, "Failed to create initial REPL module\n");
        am_runtime_destroy(g_rt);
        am_allocator_pool_destroy(g_pool);
        return 1;
    }

    am_pid_t pid = am_runtime_load_module(g_rt, mod);
    if (pid == (am_pid_t)-1) {
        fprintf(stderr, "Failed to load initial process into runtime\n");
        if (mod->ilcode) am_free(g_vm_alloc, mod->ilcode);
        if (mod->ast) am_ast_destroy(mod->ast);
        am_free(g_vm_alloc, mod);
        am_runtime_destroy(g_rt);
        am_allocator_pool_destroy(g_pool);
        return 1;
    }

    if (mod->ilcode) {
        am_free(g_vm_alloc, mod->ilcode);
        mod->ilcode = NULL;
    }
    if (mod->ast) {
        am_ast_destroy(mod->ast);
        mod->ast = NULL;
    }
    am_free(g_vm_alloc, mod);

    printf("Animac REPL\n");
    printf("输入 Scheme 表达式，空行或 Ctrl+D 退出。\n\n");

    wchar_t *session = NULL;
    size_t session_len = 0;
    wchar_t *accum = NULL;
    size_t accum_len = 0;
    int multiline = 0;

    while (!g_should_stop) {
        int indent = 0;
        int status = 0;
        if (accum_len > 0 && accum) {
            status = check_expression_complete(accum, &indent);
        }

        char prompt[64];
        if (!multiline && (!accum || accum_len == 0 || status == 0 || status == -1)) {
            snprintf(prompt, sizeof(prompt), "animac> ");
        } else {
            int spaces = indent * 2;
            if (spaces < 0) spaces = 0;
            if (spaces > 32) spaces = 32;
            snprintf(prompt, sizeof(prompt), "... %*s", spaces, "");
        }

        char *line_mb = readline(prompt);
        if (line_mb == NULL) {
            printf("\n");
            break;
        }

        if (line_mb[0] == '\0' && (!accum || accum_len == 0)) {
            free(line_mb);
            continue;
        }

        if (line_mb[0] == '\0' && accum && accum_len > 0) {
            free(line_mb);
            status = check_expression_complete(accum, &indent);
            if (status == 1) {
                continue;
            }
            if (status == -1) {
                fwprintf(stderr, L"[REPL] 括号不匹配\n");
                free(accum);
                accum = NULL;
                accum_len = 0;
                multiline = 0;
                continue;
            }
            // 追加到 session
            goto submit;
        }

        if (line_mb[0] != '\0') {
            add_history(line_mb);
        }

        wchar_t *line_w = mb_to_wchar(line_mb);
        free(line_mb);
        if (!line_w) continue;

        if (!multiline && (!accum || accum_len == 0)) {
            if (wcscmp(line_w, L"exit") == 0 ||
                wcscmp(line_w, L"quit") == 0 ||
                wcscmp(line_w, L"(exit)") == 0 ||
                wcscmp(line_w, L"(System.exit)") == 0) {
                free(line_w);
                break;
            }
        }

        size_t line_len = wcslen(line_w);
        size_t new_len = accum_len + (accum_len > 0 ? 1 : 0) + line_len;
        wchar_t *new_accum = (wchar_t *)realloc(accum, (new_len + 1) * sizeof(wchar_t));
        if (!new_accum) {
            free(line_w);
            continue;
        }
        accum = new_accum;
        if (accum_len > 0) {
            accum[accum_len] = L'\n';
            accum_len++;
        }
        memcpy(accum + accum_len, line_w, line_len * sizeof(wchar_t));
        accum_len += line_len;
        accum[accum_len] = L'\0';
        free(line_w);

        multiline = 1;
        status = check_expression_complete(accum, &indent);

        if (status == 0 && accum_len > 0) {
submit:
            // 追加到 session
            size_t need = session_len + (session_len > 0 ? 1 : 0) + accum_len;
            wchar_t *new_session = (wchar_t *)realloc(session, (need + 1) * sizeof(wchar_t));
            if (!new_session) {
                fwprintf(stderr, L"[REPL] 内存不足\n");
                free(accum);
                accum = NULL;
                accum_len = 0;
                multiline = 0;
                continue;
            }
            session = new_session;
            if (session_len > 0) {
                session[session_len] = L'\n';
                session_len++;
            }
            memcpy(session + session_len, accum, accum_len * sizeof(wchar_t));
            session_len += accum_len;
            session[session_len] = L'\0';

            size_t submitted_len = accum_len;
            free(accum);
            accum = NULL;
            accum_len = 0;
            multiline = 0;

            // 编译执行新的 session
            am_module_t *new_mod = NULL;
            am_process_t *new_proc = NULL;
            if (repl_eval_session(session, &new_mod, &new_proc) != 0) {
                // 执行失败：回滚 session 到最后一次成功状态
                if (session_len > submitted_len) {
                    session_len -= submitted_len + 1; // 去掉 '\n' + 本次输入
                } else {
                    session_len = 0;
                }
                session[session_len] = L'\0';
                continue;
            }

            // 加载并执行新进程；若运行时出错则回滚 session，保留旧进程
            g_runtime_error = 0;
            am_pid_t new_pid = am_runtime_load_module(g_rt, new_mod);

            if (new_pid == (am_pid_t)-1) {
                if (new_mod->ilcode) {
                    am_free(g_vm_alloc, new_mod->ilcode);
                    new_mod->ilcode = NULL;
                }
                if (new_mod->ast) {
                    am_ast_destroy(new_mod->ast);
                    new_mod->ast = NULL;
                }
                am_free(g_vm_alloc, new_mod);
                fwprintf(stderr, L"[REPL] 加载新进程失败\n");
                break;
            }

            if (new_mod->ilcode) {
                am_free(g_vm_alloc, new_mod->ilcode);
                new_mod->ilcode = NULL;
            }
            if (new_mod->ast) {
                am_ast_destroy(new_mod->ast);
                new_mod->ast = NULL;
            }
            am_free(g_vm_alloc, new_mod);

            int32_t running = 1;
            while (running && !g_should_stop) {
                running = am_runtime_tick(g_rt, g_rt->timeslice);
            }

            if (g_runtime_error) {
                // 运行时错误：杀掉新进程，回滚本次输入，保留旧进程
                am_runtime_kill_process(g_rt, new_pid);
                if (session_len > submitted_len) {
                    session_len -= submitted_len + 1;
                } else {
                    session_len = 0;
                }
                session[session_len] = L'\0';
                continue;
            }

            // 成功：替换为新的持久状态
            am_runtime_kill_process(g_rt, pid);
            pid = new_pid;
        } else if (status == -1) {
            fwprintf(stderr, L"[REPL] 括号不匹配\n");
            free(accum);
            accum = NULL;
            accum_len = 0;
            multiline = 0;
        }
    }

    if (accum) free(accum);
    if (session) free(session);

    am_runtime_kill_process(g_rt, pid);
    am_runtime_destroy(g_rt);
    am_allocator_pool_destroy(g_pool);

    return 0;
}
