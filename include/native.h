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

// 运行时查表：根据库名和函数名查找对应的Native函数实现。
// 成功返回函数指针，失败返回NULL。
am_native_func_t am_native_find_func(const wchar_t *lib_name, const wchar_t *func_name);


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////
//  Native Library : System
////////////////////////////////////////////////////////////////////////////

// Native函数命名规范：am_native_<LibID>_<funcName>，对应LibID.funcName。
int32_t am_native_System_test(am_runtime_t *rt, am_process_t *proc);



////////////////////////////////////////////////////////////////////////////
//  Native Library : Math
////////////////////////////////////////////////////////////////////////////

// (Math.PI) : Number
int32_t am_native_Math_PI(am_runtime_t *rt, am_process_t *proc);

// (Math.pow base:Number exponent:Number) : Number
int32_t am_native_Math_pow(am_runtime_t *rt, am_process_t *proc);

// (Math.sqrt x:Number) : Number
int32_t am_native_Math_sqrt(am_runtime_t *rt, am_process_t *proc);

// (Math.exp x:Number) : Number
int32_t am_native_Math_exp(am_runtime_t *rt, am_process_t *proc);

// (Math.log x:Number) : Number
int32_t am_native_Math_log(am_runtime_t *rt, am_process_t *proc);

// (Math.log10 x:Number) : Number
int32_t am_native_Math_log10(am_runtime_t *rt, am_process_t *proc);

// (Math.log2 x:Number) : Number
int32_t am_native_Math_log2(am_runtime_t *rt, am_process_t *proc);

// (Math.sin x:Number) : Number
int32_t am_native_Math_sin(am_runtime_t *rt, am_process_t *proc);

// (Math.cos x:Number) : Number
int32_t am_native_Math_cos(am_runtime_t *rt, am_process_t *proc);

// (Math.tan x:Number) : Number
int32_t am_native_Math_tan(am_runtime_t *rt, am_process_t *proc);

// (Math.atan x:Number) : Number
int32_t am_native_Math_atan(am_runtime_t *rt, am_process_t *proc);

// (Math.floor x:Number) : Number
int32_t am_native_Math_floor(am_runtime_t *rt, am_process_t *proc);

// (Math.ceil x:Number) : Number
int32_t am_native_Math_ceil(am_runtime_t *rt, am_process_t *proc);

// (Math.round x:Number) : Number
int32_t am_native_Math_round(am_runtime_t *rt, am_process_t *proc);

// (Math.to_fixed x:Number n:Number) : Number
int32_t am_native_Math_to_fixed(am_runtime_t *rt, am_process_t *proc);

// (Math.abs x:Number) : Number
int32_t am_native_Math_abs(am_runtime_t *rt, am_process_t *proc);

// (Math.random) : Number
int32_t am_native_Math_random(am_runtime_t *rt, am_process_t *proc);





#ifdef __cplusplus
}
#endif

#endif
