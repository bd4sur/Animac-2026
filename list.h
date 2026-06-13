#ifndef __AM_LIST_H__
#define __AM_LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "allocator.h"


///////////////////////////////////////////
// 对象语言数据对象：各类列表
///////////////////////////////////////////

// List子类型，决定了编译器和虚拟机如何解释List对象，这是Homoiconicity的基石
#define AM_LIST_TYPE_DEFAULT     (0) // 一般的运行时对象
#define AM_LIST_TYPE_LAMBDA      (1) // TODO lambda的对象布局不做特殊处理，以实现Homoiconicity
#define AM_LIST_TYPE_APPLICATION (2) // 实际等同于AM_SLIST_TYPE_DEFAULT
#define AM_LIST_TYPE_QUOTE       (3)
#define AM_LIST_TYPE_QUASIQUOTE  (4)
#define AM_LIST_TYPE_UNQUOTE     (5)

// List对象（容量固定）
typedef struct am_obj_list_t {
    am_object_t base;

    size_t      capacity;   // children容量
    size_t      length;     // children元素个数（最后一个元素的下标）
    int32_t     type;       // List子类型（AM_LIST_TYPE_*）
    am_handle_t parent;     // 亲list的把柄
    am_value_t  children[]; // Array<am_value_t> 柔性数组
} am_obj_list_t;


am_obj_list_t *am_obj_list_create(am_allocator_t *alloc, size_t capacity, int32_t type, am_handle_t parent);

int32_t am_obj_list_destroy(am_allocator_t *alloc, am_obj_list_t *lst);

am_value_t am_obj_list_get(am_allocator_t *alloc, am_obj_list_t *lst, size_t index);

int32_t am_obj_list_set(am_allocator_t *alloc, am_obj_list_t *lst, size_t index, am_value_t item);

int32_t am_obj_list_push(am_allocator_t *alloc, am_obj_list_t *lst, am_value_t item);

am_value_t am_obj_list_pop(am_allocator_t *alloc, am_obj_list_t *lst);



#ifdef __cplusplus
}
#endif

#endif
