#ifndef __AM_OBJECT_H__
#define __AM_OBJECT_H__

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

typedef uint64_t am_handle_t;
typedef uint64_t am_varid_t;
typedef uint64_t am_iaddr_t;


typedef struct am_list_t am_list_t; // Array<am_value_t>
typedef struct am_map_t am_map_t; // opaque forward declaration

// NOTE AM_VALUE_TYPE_BOOLEAN AM_VALUE_TYPE_SYMBOL AM_VALUE_TYPE_UNDEFINED AM_VALUE_TYPE_NULL 做成全局单例

typedef enum am_value_type_t {
    AM_VALUE_TYPE_BOOLEAN,
    AM_VALUE_TYPE_NUMBER,
    AM_VALUE_TYPE_SYMBOL,
    AM_VALUE_TYPE_WCHAR, // 仅用于组成字符串
    AM_VALUE_TYPE_UNDEFINED,
    AM_VALUE_TYPE_NULL,
    AM_VALUE_TYPE_IADDR,
    AM_VALUE_TYPE_HANDLE,
} am_value_type_t;

typedef enum am_object_type_t {
    AM_OBJECT_TYPE_LAMBDA,
    AM_OBJECT_TYPE_APPLICATION,
    AM_OBJECT_TYPE_QUOTE,
    AM_OBJECT_TYPE_QUASIQUOTE,
    AM_OBJECT_TYPE_UNQUOTE,
    AM_OBJECT_TYPE_STRING,
    AM_OBJECT_TYPE_CLOSURE,
    AM_OBJECT_TYPE_CONTINUATION,
} am_object_type_t;

typedef struct am_value_t {
    am_value_type_t   type;
    union {
        double   number; // NUMBER
        uint64_t index;  // BOOLEAN/SYMBOL/IADDR/HANDLE/UNDEFINED/NULL
        wchar_t  wch;    // WCHAR
    } data;
} am_value_t;


typedef struct am_process_snapshot_t {
    am_handle_t current_closure_handle;
    am_list_t  *opstack; // Array<value>
    am_list_t  *fstack;  // Array<frame>
} am_process_snapshot_t; // 用于continuation保存当前计算续体



typedef struct am_object_t {
    uint64_t         header; // TODO 预留
    am_object_type_t type;
    am_handle_t      parent;
    am_list_t       *children;
    uint64_t         length;
    // closure
    am_iaddr_t       iaddr;
    am_map_t        *bound; // am_varid_t -> *am_value_t
    am_map_t        *free; // am_varid_t -> *am_value_t
    am_map_t        *dirty_flag; // am_varid_t -> int32_t
    // continuation
    am_iaddr_t       cont_return_target;
    am_process_snapshot_t *snapshot;
} am_object_t;





typedef struct am_frame_t {
    am_handle_t handle_to_closure_object;
    am_iaddr_t  iaddr;
} am_frame_t;


#endif