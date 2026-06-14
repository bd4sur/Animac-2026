#ifndef __AM_WSTRING_H__
#define __AM_WSTRING_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "allocator.h"


// 宽字符串（不可扩容）：同时作为基础数据结构和语言数据对象
// NOTE 说明：虽然是基础数据结构，但实质上可作为对象语言的数据对象。详见am_map_t的说明。
typedef struct am_wstring_t {
    am_object_t base;

    size_t     length;     // content字符个数（最后一个字符的下标+1）=content容量
    am_value_t content[];  // Array<am_value_t(am_wchar_t)> 柔性数组
} am_wstring_t;








#ifdef __cplusplus
}
#endif

#endif
