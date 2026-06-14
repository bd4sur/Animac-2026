#ifndef __AM_FRAME_H__
#define __AM_FRAME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "allocator.h"


// 栈帧类型
typedef struct am_obj_frame_t {
    am_object_t base;

    am_value_t handle_to_closure_object;
    am_value_t iaddr;
} am_obj_frame_t;








#ifdef __cplusplus
}
#endif

#endif
