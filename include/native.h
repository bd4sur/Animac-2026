#ifndef __AM_NATIVE_H__
#define __AM_NATIVE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>

#include "runtime.h"


// Native函数指针类型：与op_*指令函数签名一致
typedef int32_t (*am_native_func_t)(am_runtime_t *rt, am_process_t *proc);


// 函数表项：库内的单个函数
typedef struct {
    const wchar_t *name; // 函数名（suffix）
    am_native_func_t func;
} am_native_func_entry_t;


// 库表项：一个native库及其函数表
typedef struct {
    const wchar_t *name;                  // 库名（prefix / native_id）
    const am_native_func_entry_t *funcs;  // 该库的函数表
    size_t func_count;                    // 函数表长度
} am_native_lib_entry_t;


// 运行时查表：根据库名和函数名查找对应的Native函数实现。
// 成功返回函数指针，失败返回NULL。
am_native_func_t am_native_find_func(const wchar_t *lib_name, const wchar_t *func_name);


////////////////////////////////////////////////////////////////////////////
//  Native Library : System
////////////////////////////////////////////////////////////////////////////

// 由 src/native.c 定义
extern const am_native_lib_entry_t am_native_System_lib;

// Native函数命名规范：am_native_<LibID>_<funcName>，对应LibID.funcName。
int32_t am_native_System_test(am_runtime_t *rt, am_process_t *proc);



#ifdef __cplusplus
}
#endif

#endif
