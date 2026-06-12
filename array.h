#ifndef __AM_ARRAY_H__
#define __AM_ARRAY_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "allocator.h"

///////////////////////////////////////////
// 基础数据结构：通用线性表 am_array_t<am_value_t>
///////////////////////////////////////////

// 线性表
typedef struct am_array_t {
    uint32_t length;
    am_value_t arr[];
} am_array_t;

#endif
