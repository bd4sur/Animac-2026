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

// Native函数命名规范：am_native_<LibID>_<funcName>，对应LibID.funcName。
int32_t am_native_System_test(am_runtime_t *rt, am_process_t *proc);



#ifdef __cplusplus
}
#endif

#endif
