#ifndef __AM_CONTINUATION_H__
#define __AM_CONTINUATION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "allocator.h"


// 续体类型
typedef struct am_obj_continuation_t {
    am_object_t base;

    am_value_t cont_return_target; // iaddr
    am_value_t current_closure_handle; // handle
    // am_obj_array_t *opstack; // Array<value>
    // am_obj_array_t *fstack;  // Array<value>
} am_obj_continuation_t;








#ifdef __cplusplus
}
#endif

#endif
