#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <assert.h>
#include <unistd.h>

#include "utils.h"
#include "parser.h"
#include "linker.h"
#include "compiler.h"
#include "module.h"
#include "process.h"
#include "runtime.h"
#include "debug.h"
#include "ast.h"

#include "native_System.h"
#include "native_Math.h"
#include "native_String.h"
#include "native_Table.h"

#ifndef AM_ALLOCATOR_POOL_SIZE
#define AM_ALLOCATOR_POOL_SIZE ((size_t)(128ULL * 1024 * 1024))
#endif

#define AM_REPL_VERSION L"2026.7"
#define AM_REPL_BASE_DIR_BUF_SIZE 512

static am_allocator_pool_t *g_pool = NULL;
static am_allocator_t *vm_alloc = NULL;
static am_allocator_t *heap_alloc = NULL;

static wchar_t g_base_dir_w[AM_REPL_BASE_DIR_BUF_SIZE];

static wchar_t *g_all_code = NULL;
static size_t g_all_code_len = 0;
static size_t g_all_code_cap = 0;

typedef struct {
    wchar_t **lines;
    size_t length;
    size_t capacity;
} am_repl_input_buffer_t;

static am_repl_input_buffer_t g_input_buffer = { NULL, 0, 0 };

static int g_running = 1;
static int g_should_print_prompt = 1;

// ===============================================================================
// 宽字符串工具
// ===============================================================================

static wchar_t *am_repl_wcs_dup(const wchar_t *s) {
    if (!s) return NULL;
    size_t len = wcslen(s);
    wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!buf) return NULL;
    memcpy(buf, s, (len + 1) * sizeof(wchar_t));
    return buf;
}

static wchar_t *am_repl_wcs_trim(const wchar_t *s) {
    if (!s) return am_repl_wcs_dup(L"");
    const wchar_t *start = s;
    while (*start && iswspace((wint_t)*start)) start++;
    const wchar_t *end = s + wcslen(s);
    while (end > start && iswspace((wint_t)*(end - 1))) end--;
    size_t len = (size_t)(end - start);
    wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!buf) return NULL;
    memcpy(buf, start, len * sizeof(wchar_t));
    buf[len] = L'\0';
    return buf;
}

// ===============================================================================
// 输入缓冲区
// ===============================================================================

static int32_t am_repl_input_buffer_push(am_repl_input_buffer_t *buf, wchar_t *line) {
    if (!buf || !line) return -1;
    if (buf->length >= buf->capacity) {
        size_t new_cap = buf->capacity ? buf->capacity * 2 : 4;
        wchar_t **new_lines = (wchar_t **)realloc(buf->lines, new_cap * sizeof(wchar_t *));
        if (!new_lines) return -1;
        buf->lines = new_lines;
        buf->capacity = new_cap;
    }
    buf->lines[buf->length++] = line;
    return 0;
}

static void am_repl_input_buffer_clear(am_repl_input_buffer_t *buf) {
    if (!buf) return;
    for (size_t i = 0; i < buf->length; i++) {
        free(buf->lines[i]);
        buf->lines[i] = NULL;
    }
    buf->length = 0;
}

static wchar_t *am_repl_input_buffer_join(const am_repl_input_buffer_t *buf) {
    if (!buf) return NULL;
    size_t total = 0;
    for (size_t i = 0; i < buf->length; i++) {
        total += wcslen(buf->lines[i]);
    }
    wchar_t *result = (wchar_t *)malloc((total + 1) * sizeof(wchar_t));
    if (!result) return NULL;
    size_t pos = 0;
    for (size_t i = 0; i < buf->length; i++) {
        size_t len = wcslen(buf->lines[i]);
        memcpy(result + pos, buf->lines[i], len * sizeof(wchar_t));
        pos += len;
    }
    result[pos] = L'\0';
    return result;
}

// ===============================================================================
// 历史代码缓冲区
// ===============================================================================

static int32_t am_repl_append_all_code(const wchar_t *code) {
    if (!code) return -1;
    size_t len = wcslen(code);
    size_t need = g_all_code_len + len + 2; // 结尾空格 + 结尾 '\0'
    if (need > g_all_code_cap) {
        size_t new_cap = g_all_code_cap ? g_all_code_cap * 2 : 1024;
        while (new_cap < need) new_cap *= 2;
        wchar_t *new_buf = (wchar_t *)realloc(g_all_code, new_cap * sizeof(wchar_t));
        if (!new_buf) return -1;
        g_all_code = new_buf;
        g_all_code_cap = new_cap;
    }
    for (size_t i = 0; i < len; i++) {
        g_all_code[g_all_code_len++] = code[i];
    }
    g_all_code[g_all_code_len++] = L' ';
    g_all_code[g_all_code_len] = L'\0';
    return 0;
}

static void am_repl_clear_all_code(void) {
    free(g_all_code);
    g_all_code = NULL;
    g_all_code_len = 0;
    g_all_code_cap = 0;
}

// ===============================================================================
// 括号计数与副作用语句检测
// ===============================================================================

static int32_t am_repl_count_brackets(const wchar_t *s) {
    if (!s) return 0;
    int32_t count = 0;
    for (size_t i = 0; s[i] != L'\0'; i++) {
        if (s[i] == L'(' || s[i] == L'{') count++;
        else if (s[i] == L')' || s[i] == L'}') count--;
    }
    return count;
}

static bool am_repl_should_retain_input(const wchar_t *input) {
    if (!input) return false;
    return wcsstr(input, L"define") != NULL ||
           wcsstr(input, L"set!") != NULL ||
           wcsstr(input, L"native") != NULL ||
           wcsstr(input, L"import") != NULL;
}

// ===============================================================================
// 运行时回调
// ===============================================================================

static void am_repl_flush_output_fifo(am_runtime_t *rt) {
    if (!rt || !rt->output_fifo) return;
    while (rt->output_fifo->length > 0) {
        am_value_t v = am_list_shift(rt->vm_alloc, rt->output_fifo);
        if (am_value_is_wchar(v)) {
            putwchar((wchar_t)am_value_to_wchar(v));
        }
    }
    fflush(stdout);
}

static void am_repl_on_tick(am_runtime_t *rt) {
    am_repl_flush_output_fifo(rt);
}

static void am_repl_on_error(am_runtime_t *rt) {
    (void)rt;
}

static void am_repl_on_halt(am_runtime_t *rt) {
    am_repl_flush_output_fifo(rt);
    am_process_t *proc = am_runtime_get_process(rt, 0);
    if (proc && proc->state == AM_PROCESS_STATE_SLEEPING) {
        return;
    }
    g_should_print_prompt = 1;
}

// ===============================================================================
// 执行用户输入
// ===============================================================================

static int32_t am_repl_run(const wchar_t *code) {
    if (!code) return -1;

    const wchar_t *prefix = L"((lambda () \n";
    const wchar_t *display_prefix = L"(display ";
    const wchar_t *display_suffix = L") (newline)";
    const wchar_t *suffix = L"\n))";

    size_t prefix_len = wcslen(prefix);
    size_t dp_len = wcslen(display_prefix);
    size_t ds_len = wcslen(display_suffix);
    size_t suffix_len = wcslen(suffix);
    size_t all_len = g_all_code_len;
    size_t code_len = wcslen(code);
    size_t total = prefix_len + all_len + dp_len + code_len + ds_len + suffix_len;

    wchar_t *full_code = (wchar_t *)am_malloc(vm_alloc, (total + 1) * sizeof(wchar_t));
    if (!full_code) return -1;

    size_t pos = 0;
    memcpy(full_code + pos, prefix, prefix_len * sizeof(wchar_t));
    pos += prefix_len;
    if (all_len > 0) {
        memcpy(full_code + pos, g_all_code, all_len * sizeof(wchar_t));
        pos += all_len;
    }
    memcpy(full_code + pos, display_prefix, dp_len * sizeof(wchar_t));
    pos += dp_len;
    memcpy(full_code + pos, code, code_len * sizeof(wchar_t));
    pos += code_len;
    memcpy(full_code + pos, display_suffix, ds_len * sizeof(wchar_t));
    pos += ds_len;
    memcpy(full_code + pos, suffix, suffix_len * sizeof(wchar_t));
    pos += suffix_len;
    full_code[pos] = L'\0';

    size_t base_len = wcslen(g_base_dir_w);
    const wchar_t *repl_name = L"/repl.scm";
    size_t name_len = wcslen(repl_name);
    wchar_t *module_path = (wchar_t *)am_malloc(vm_alloc, (base_len + name_len + 1) * sizeof(wchar_t));
    if (!module_path) {
        am_allocator_pool_reset_vm(g_pool);
        return -1;
    }
    pos = 0;
    if (base_len > 0) {
        memcpy(module_path + pos, g_base_dir_w, base_len * sizeof(wchar_t));
        pos += base_len;
    }
    memcpy(module_path + pos, repl_name, name_len * sizeof(wchar_t));
    pos += name_len;
    module_path[pos] = L'\0';

    am_ast_t *ast = am_parse(vm_alloc, full_code, module_path, 0);
    if (!ast) {
        fwprintf(stderr, L"[REPL Error] 解析失败\n");
        fflush(stderr);
        am_allocator_pool_reset_vm(g_pool);
        return -1;
    }

    am_ast_t *linked = am_link(ast, g_base_dir_w);
    if (!linked) {
        fwprintf(stderr, L"[REPL Error] 链接失败\n");
        fflush(stderr);
        am_allocator_pool_reset_vm(g_pool);
        return -1;
    }

    am_module_t *mod = am_compile(linked, 0, 0);
    if (!mod) {
        fwprintf(stderr, L"[REPL Error] 编译失败\n");
        fflush(stderr);
        am_allocator_pool_reset_vm(g_pool);
        return -1;
    }

    size_t dump_size = am_module_dump(vm_alloc, vm_alloc, mod, NULL, 0);
    if (dump_size == SIZE_MAX) {
        fwprintf(stderr, L"[REPL Error] 模块转储失败\n");
        fflush(stderr);
        am_allocator_pool_reset_vm(g_pool);
        return -1;
    }

    uint8_t *module_buffer = (uint8_t *)malloc(dump_size);
    if (!module_buffer) {
        am_allocator_pool_reset_vm(g_pool);
        return -1;
    }
    memset(module_buffer, 0, dump_size);

    size_t written = am_module_dump(vm_alloc, vm_alloc, mod, module_buffer, 0);
    if (written != dump_size) {
        fwprintf(stderr, L"[REPL Error] 模块转储写入失败\n");
        fflush(stderr);
        free(module_buffer);
        am_allocator_pool_reset_vm(g_pool);
        return -1;
    }

    am_allocator_pool_reset_vm(g_pool);

    am_module_t *mod_loaded = am_module_load(vm_alloc, heap_alloc, module_buffer, 0);
    if (!mod_loaded) {
        fwprintf(stderr, L"[REPL Error] 模块加载失败\n");
        fflush(stderr);
        free(module_buffer);
        return -1;
    }

    am_runtime_t *rt = am_runtime_create(vm_alloc, heap_alloc, g_base_dir_w);
    if (!rt) {
        fwprintf(stderr, L"[REPL Error] 运行时创建失败\n");
        fflush(stderr);
        free(module_buffer);
        return -1;
    }

    am_runtime_register_native_lib(rt, &am_native_System_lib);
    am_runtime_register_native_lib(rt, &am_native_Math_lib);
    am_runtime_register_native_lib(rt, &am_native_String_lib);
    am_runtime_register_native_lib(rt, &am_native_Table_lib);

    rt->callback_on_tick = am_repl_on_tick;
    rt->callback_on_halt = am_repl_on_halt;
    rt->callback_on_error = am_repl_on_error;

    am_pid_t pid = am_runtime_load_module(rt, mod_loaded);
    if (pid == (am_pid_t)-1) {
        fwprintf(stderr, L"[REPL Error] 加载模块到运行时失败\n");
        fflush(stderr);
        am_runtime_destroy(rt);
        free(module_buffer);
        return -1;
    }

    g_should_print_prompt = 0;
    am_runtime_start(rt);
    am_repl_flush_output_fifo(rt);

    am_runtime_destroy(rt);
    free(module_buffer);

    return 0;
}

// ===============================================================================
// REPL 命令与行读取
// ===============================================================================

static void am_repl_print_help(void) {
    wprintf(L"Animac Scheme V%ls\n", AM_REPL_VERSION);
    wprintf(L"Copyright (c) 2018~2026 BD4SUR\n");
    wprintf(L"https://github.com/bd4sur/Animac\n");
    wprintf(L"\n");
    wprintf(L"REPL Command Reference:\n");
    wprintf(L"  .exit     exit the REPL.\n");
    wprintf(L"  .reset    reset the REPL to initial state.\n");
    wprintf(L"  .help     show usage and copyright information.\n");
    wprintf(L"\n");
    fflush(stdout);
}

static void am_repl_print_continuation_prompt(int32_t indent_level) {
    wchar_t prompt[64];
    prompt[0] = L'\0';
    wcscat(prompt, L"...");
    for (int32_t i = 1; i < indent_level; i++) {
        wcscat(prompt, L"..");
    }
    wcscat(prompt, L" ");
    wprintf(L"%ls", prompt);
    fflush(stdout);
}

static void am_repl_eval(const wchar_t *input) {
    if (!input) return;

    wchar_t *trimmed = am_repl_wcs_trim(input);
    if (!trimmed) return;

    if (wcscmp(trimmed, L".help") == 0) {
        am_repl_print_help();
        free(trimmed);
        g_should_print_prompt = 1;
        return;
    }
    else if (wcscmp(trimmed, L".exit") == 0) {
        free(trimmed);
        g_running = 0;
        return;
    }
    else if (wcscmp(trimmed, L".reset") == 0) {
        am_repl_clear_all_code();
        am_repl_input_buffer_clear(&g_input_buffer);
        wprintf(L"REPL已重置。\n");
        fflush(stdout);
        free(trimmed);
        g_should_print_prompt = 1;
        return;
    }
    free(trimmed);

    wchar_t *line_copy = am_repl_wcs_dup(input);
    if (!line_copy) return;
    if (am_repl_input_buffer_push(&g_input_buffer, line_copy) != 0) {
        free(line_copy);
        return;
    }

    wchar_t *code = am_repl_input_buffer_join(&g_input_buffer);
    if (!code) return;

    int32_t indent_level = am_repl_count_brackets(code);
    if (indent_level > 0) {
        am_repl_print_continuation_prompt(indent_level);
        free(code);
        g_should_print_prompt = 0;
        return;
    }
    else if (indent_level < 0) {
        wprintf(L"[REPL Error] 括号不匹配\n");
        fflush(stdout);
        am_repl_input_buffer_clear(&g_input_buffer);
        free(code);
        g_should_print_prompt = 1;
        return;
    }

    am_repl_input_buffer_clear(&g_input_buffer);

    int32_t run_ok = am_repl_run(code);
    if (run_ok == 0) {
        if (am_repl_should_retain_input(code)) {
            am_repl_append_all_code(code);
        }
    }
    else {
        if (wcsstr(code, L"define") != NULL) {
            am_repl_append_all_code(code);
        }
    }
    free(code);
}

static wchar_t *am_repl_read_line(void) {
    size_t cap = 256;
    wchar_t *buf = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (!buf) return NULL;
    size_t len = 0;
    wint_t c;
    while ((c = getwchar()) != WEOF && c != L'\n') {
        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            wchar_t *new_buf = (wchar_t *)realloc(buf, new_cap * sizeof(wchar_t));
            if (!new_buf) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
            cap = new_cap;
        }
        buf[len++] = (wchar_t)c;
    }
    if (c == WEOF && len == 0) {
        free(buf);
        return NULL;
    }
    buf[len] = L'\0';
    return buf;
}

// ===============================================================================
// 入口
// ===============================================================================

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (!setlocale(LC_ALL, "")) {
        fprintf(stderr, "setlocale failed\n");
        return 1;
    }

    g_pool = am_allocator_pool_create(AM_ALLOCATOR_POOL_SIZE);
    if (!g_pool) {
        fprintf(stderr, "Failed to create allocator pool\n");
        return 1;
    }
    vm_alloc = am_allocator_pool_get_vm(g_pool);
    heap_alloc = am_allocator_pool_get_heap(g_pool);

    char cwd[AM_REPL_BASE_DIR_BUF_SIZE];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "Failed to get current working directory\n");
        am_allocator_pool_destroy(g_pool);
        return 1;
    }
    _mbstowcs(g_base_dir_w, cwd, AM_REPL_BASE_DIR_BUF_SIZE - 1);

    wprintf(L"Animac Scheme V%ls\n", AM_REPL_VERSION);
    wprintf(L"Copyright (c) 2018~2026 BD4SUR\n");
    wprintf(L"Type \".help\" for more information.\n");
    wprintf(L"> ");
    fflush(stdout);
    g_should_print_prompt = 0;

    while (g_running) {
        wchar_t *line = am_repl_read_line();
        if (!line) {
            break;
        }
        am_repl_eval(line);
        free(line);

        if (g_should_print_prompt) {
            wprintf(L"> ");
            fflush(stdout);
            g_should_print_prompt = 0;
        }
    }

    am_repl_clear_all_code();
    am_repl_input_buffer_clear(&g_input_buffer);
    free(g_input_buffer.lines);

    am_allocator_pool_destroy(g_pool);
    g_pool = NULL;
    vm_alloc = NULL;
    heap_alloc = NULL;

    return 0;
}
