#ifndef __AM_NATIVE_SYSTEM_H__
#define __AM_NATIVE_SYSTEM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "native.h"


////////////////////////////////////////////////////////////////////////////
//  Native Library : System
////////////////////////////////////////////////////////////////////////////

// 由 src/native_System.c 定义
extern const am_native_lib_entry_t am_native_System_lib;


// (System.exec cmd:String) : String
// 同步执行 shell 命令，返回标准输出字符串。
int32_t am_native_System_exec(am_runtime_t *rt, am_process_t *proc);

// (System.set_timeout time_ms:Number callback:(void->undefined)) : Number
int32_t am_native_System_set_timeout(am_runtime_t *rt, am_process_t *proc);

// (System.set_interval time_ms:Number callback:(void->undefined)) : Number
int32_t am_native_System_set_interval(am_runtime_t *rt, am_process_t *proc);

// (System.clear_timeout timer:Number) : void
int32_t am_native_System_clear_timeout(am_runtime_t *rt, am_process_t *proc);

// (System.clear_interval timer:Number) : void
int32_t am_native_System_clear_interval(am_runtime_t *rt, am_process_t *proc);

// (System.timestamp) : Number
int32_t am_native_System_timestamp(am_runtime_t *rt, am_process_t *proc);

// (System.test x:any) : String
int32_t am_native_System_test(am_runtime_t *rt, am_process_t *proc);



#ifdef __cplusplus
}
#endif

#endif
