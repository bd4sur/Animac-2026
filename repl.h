#ifndef __REPL_H__
#define __REPL_H__

#include <stddef.h>
#include <stdint.h>

#include "utils.h"
#include "parser.h"
#include "compiler.h"
#include "module.h"
#include "process.h"
#include "runtime.h"
#include "ast.h"
#include "list.h"
#include "vocab.h"
#include "heap.h"
#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif

// REPL 上下文状态码，供外层逻辑决定如何呈现。
typedef enum {
    REPL_STATUS_CONTINUE = 0,  // 表达式不完整，需要继续输入
    REPL_STATUS_OUTPUT,        // 产生正常输出
    REPL_STATUS_ERROR,         // 发生错误
    REPL_STATUS_EXIT,          // 请求退出
} repl_status_t;

// REPL 上下文。内部状态直接暴露给外层，便于外层根据状态决定呈现方式。
typedef struct repl_ctx {
    // 运行时基础设施
    am_allocator_pool_t *pool;
    am_allocator_t *vm_alloc;
    am_allocator_t *heap_alloc;
    am_runtime_t *rt;
    am_pid_t pid;
    volatile int runtime_error;
    volatile int should_stop;  // 由 repl_ctx_interrupt() 置位，用于中断运行

    // 会话与当前累积输入
    wchar_t *session;
    size_t session_len;
    wchar_t *accum;
    size_t accum_len;
    int multiline;

    // 输出缓冲区（由上下文拥有，有效到下一次 feed 或 destroy）
    char *output_buf;
    size_t output_cap;
    size_t output_len;

    // 模式与提示符
    int js_mode;
    const char *prompt_main;
    const char *prompt_cont;

    // 供外层读取的内部状态
    repl_status_t status;
    const char *output;  // 指向 output_buf 或 NULL
    int indent;          // 建议的续行缩进层级
} repl_ctx_t;

// 一次 feed 的返回结果。output 指向 ctx 内部缓冲区，调用者无需释放。
typedef struct {
    repl_status_t status;
    const char *output;
    int indent;
} repl_result_t;

// 建立 REPL 上下文。
repl_ctx_t *repl_ctx_create(void);

// 销毁 REPL 上下文。
void repl_ctx_destroy(repl_ctx_t *ctx);

// 中断当前上下文中的运行（例如响应 SIGINT）。
void repl_ctx_interrupt(repl_ctx_t *ctx);

// 设置 JS 解释器模式。
void repl_ctx_set_js_mode(repl_ctx_t *ctx, int js_mode);

// 向 REPL 上下文传入一个字符串（通常是一行输入），返回结果并更新上下文内部状态。
repl_result_t repl_ctx_feed(repl_ctx_t *ctx, const char *input);

#ifdef __cplusplus
}
#endif

#endif // __REPL_H__
