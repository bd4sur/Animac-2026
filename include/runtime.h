#ifndef __AM_RUNTIME_H__
#define __AM_RUNTIME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>

#include "object.h"
#include "allocator.h"
#include "process.h"
#include "module.h"
#include "list.h"

// 前向声明：回调函数指针需要引用运行时类型
typedef struct am_runtime_t am_runtime_t;


///////////////////////////////////////////
// 虚拟机状态
///////////////////////////////////////////

#define AM_VM_STATE_IDLE    (0)
#define AM_VM_STATE_RUNNING (1)


///////////////////////////////////////////
// GC 配置
///////////////////////////////////////////

#ifndef AM_ENABLE_GC
#define AM_ENABLE_GC (1)
#endif

#ifndef AM_GC_INTERVAL
#define AM_GC_INTERVAL (1) // 秒
#endif


///////////////////////////////////////////
// 运行时事件处理器配置
///////////////////////////////////////////

#ifndef AM_COMPUTATION_PHASE_LENGTH
#define AM_COMPUTATION_PHASE_LENGTH (1)
#endif


///////////////////////////////////////////
// 异步定时器基础设施
///////////////////////////////////////////

// 时间戳类型：毫秒
typedef uint64_t am_timestamp_t;

// 定时器条目（不透明类型，具体定义见 runtime.c）
typedef struct am_timer_t am_timer_t;

// 注册一个定时器。delay_ms 为首次触发的延迟，repeat 表示是否周期触发，
// interval_ms 为周期触发的间隔。成功返回大于 0 的定时器编号，失败返回 0。
size_t am_runtime_set_timer(am_runtime_t *rt, am_pid_t pid, am_handle_t callback,
                            am_timestamp_t delay_ms, bool repeat, am_timestamp_t interval_ms);

// 根据编号取消一个定时器。成功返回 true，未找到返回 false。
bool am_runtime_clear_timer(am_runtime_t *rt, size_t timer_id);

// 获取当前时间戳（毫秒）。
am_timestamp_t am_runtime_now_ms(void);

// 以异步方式调用一个闭包：压入栈帧并跳转到闭包入口，返回地址为 return_target。
// 用于定时器回调等场景。成功返回 0，失败返回 -1。
int32_t am_runtime_call_async(am_runtime_t *rt, am_process_t *proc, am_handle_t callback,
                              am_iaddr_t return_target);


///////////////////////////////////////////
// 运行时环境
// 说明：运行时是虚拟机调度的核心，管理进程池、进程队列、FIFO 和回调。
///////////////////////////////////////////

typedef struct am_runtime_t {
    am_allocator_t *vm_alloc;   // 运行时工作内存分配器
    am_allocator_t *heap_alloc; // 进程堆内存分配器

    wchar_t *working_dir;       // 基准工作目录

    size_t process_pool_capacity; // 进程池容量
    am_process_t **process_pool;  // 进程池：am_process_t* 动态数组
    size_t process_poll_counter;  // 进程计数器，也作为下一个 PID
    am_list_t *process_queue;     // 进程队列：List<am_value_t(uint:pid)>

    am_list_t *input_fifo;   // 输入 FIFO（存储 wstring 对象指针）
    am_list_t *output_fifo;  // 输出 FIFO（存储 wstring 对象指针）
    am_list_t *error_fifo;   // 错误 FIFO（存储 wstring 对象指针）

    void (*callback_on_tick)(am_runtime_t *rt);   // 每个 Tick 结束后触发
    void (*callback_on_event)(am_runtime_t *rt);  // 每个事件循环结束后触发
    void (*callback_on_halt)(am_runtime_t *rt);   // 虚拟机进入 IDLE 后触发
    void (*callback_on_error)(am_runtime_t *rt);  // 虚拟机捕获异常时触发

    size_t tick_counter;     // Tick 计数器
    time_t gc_timestamp;     // GC 时间戳

    am_timer_t *timer_list;  // 定时器链表头
    size_t timer_next_id;    // 下一个定时器编号
} am_runtime_t;


///////////////////////////////////////////
// 生命周期
///////////////////////////////////////////

// 创建运行时。成功返回运行时指针，失败返回 NULL。
// base_dir 为基准工作目录，允许为 NULL。
am_runtime_t *am_runtime_create(am_allocator_t *vm_alloc, am_allocator_t *heap_alloc, const wchar_t *base_dir);

// 销毁运行时，释放其占用的全部资源。成功返回 0，失败返回 -1。
int32_t am_runtime_destroy(am_runtime_t *rt);


///////////////////////////////////////////
// Native 库注册
///////////////////////////////////////////

// 前向声明：完整定义见 native.h
typedef struct am_native_lib_entry_t am_native_lib_entry_t;

// 向运行时注册一个native库。成功返回0，失败返回-1。
int32_t am_runtime_register_native_lib(am_runtime_t *rt, const am_native_lib_entry_t *lib);


///////////////////////////////////////////
// 入口函数（兼容参考用法）
///////////////////////////////////////////

// 将模块加载到运行时中，创建并启动一个进程。成功返回 PID，失败返回 -1。
am_pid_t am_load_module(am_runtime_t *rt, am_module_t *mod);

// 启动虚拟机主循环，直到所有进程执行结束进入 IDLE。
void am_start(am_runtime_t *rt);


///////////////////////////////////////////
// 模块与进程管理
///////////////////////////////////////////

// 将模块加载到运行时中。成功返回 PID，失败返回 (am_pid_t)-1。
am_pid_t am_runtime_load_module(am_runtime_t *rt, am_module_t *mod);

// 根据 PID 获取进程。成功返回进程指针，失败返回 NULL。
am_process_t *am_runtime_get_process(am_runtime_t *rt, am_pid_t pid);


///////////////////////////////////////////
// 调度器
///////////////////////////////////////////

// 执行一次事件循环：执行若干 Tick，触发 GC 和事件回调。
// 返回 AM_VM_STATE_IDLE 或 AM_VM_STATE_RUNNING。
int32_t am_runtime_event_handler(am_runtime_t *rt);

// 执行一个时间片。返回 AM_VM_STATE_IDLE 或 AM_VM_STATE_RUNNING。
int32_t am_runtime_tick(am_runtime_t *rt, uint32_t timeslice);

// 根据opcode和operand分派具体的执行逻辑（指令译码）
int32_t am_runtime_op_dispatch(am_runtime_t *rt, am_process_t *proc, uint32_t opcode, am_value_t operand);

// 执行当前进程的一条指令。成功返回 0，失败返回 -1。
int32_t am_runtime_execute(am_runtime_t *rt, am_process_t *proc);

// 启动虚拟机主循环。
void am_runtime_start(am_runtime_t *rt);


///////////////////////////////////////////
// 控制台输入输出
///////////////////////////////////////////

// 向 stdout 输出字符串，并记录到 output_fifo。
void am_runtime_output(am_runtime_t *rt, const wchar_t *str);

// 向 stderr 输出字符串，并记录到 error_fifo。
void am_runtime_error(am_runtime_t *rt, const wchar_t *str);


#ifdef __cplusplus
}
#endif

#endif
