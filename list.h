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
// 基础数据结构：通用线性表 am_list_t<am_value_t>
///////////////////////////////////////////

// List子类型，决定了编译器和虚拟机如何解释List对象，这是Homoiconicity的基石
#define AM_LIST_TYPE_DEFAULT     (0) // 一般的运行时对象
#define AM_LIST_TYPE_LAMBDA      (1) // TODO lambda的对象布局不做特殊处理，以实现Homoiconicity
#define AM_LIST_TYPE_APPLICATION (2) // 实际等同于AM_SLIST_TYPE_DEFAULT
#define AM_LIST_TYPE_QUOTE       (3)
#define AM_LIST_TYPE_QUASIQUOTE  (4)
#define AM_LIST_TYPE_UNQUOTE     (5)

// List对象（动态扩容）
// NOTE 说明：am_list_t虽然是基础数据结构，但实质上可作为对象语言的数据对象。详见am_map_t的说明。
typedef struct am_list_t {
    am_object_t base;

    size_t      capacity;   // children容量
    size_t      length;     // children元素个数（最后一个元素的下标）
    int32_t     type;       // List子类型（AM_LIST_TYPE_*）
    am_handle_t parent;     // 亲list的把柄
    am_value_t  children[]; // Array<am_value_t> 柔性数组
} am_list_t;

// 遍历回调类型
typedef void (*am_list_iter_callback_t)(size_t index, am_value_t item, void *user_data);

// NOTE 动态扩容策略参考
//      cpython：new_allocated = ((size_t)newsize + (newsize >> 3) + 6) & ~(size_t)3;
//      v8：old_capacity * 1.5 + 16


am_list_t *am_list_create(am_allocator_t *alloc, size_t capacity, int32_t type, am_handle_t parent); // V8采用的默认初始容量是4

int32_t am_list_destroy(am_allocator_t *alloc, am_list_t *lst);

am_list_t *am_list_copy(am_allocator_t *alloc, am_list_t *lst);

void am_list_iter(am_allocator_t *alloc, am_list_t *lst, am_list_iter_callback_t cb, void *user_data);

// 将lst的二进制内存布局从alloc管理的内存中倒出来，返回一个系统malloc的二进制序列，以及序列长度
//   注意：压缩对象，将capacity压缩到跟length一致，删除多余分配的空闲部分
uint8_t *am_list_dump(am_allocator_t *alloc, am_list_t *lst, size_t *size);



am_value_t am_list_get(am_allocator_t *alloc, am_list_t *lst, size_t index);

int32_t am_list_set(am_allocator_t *alloc, am_list_t *lst, size_t index, am_value_t item);

am_list_t *am_list_push(am_allocator_t *alloc, am_list_t *lst, am_value_t item); // 带动态扩容

am_value_t am_list_pop(am_allocator_t *alloc, am_list_t *lst);

am_value_t am_list_shift(am_allocator_t *alloc, am_list_t *lst); // 弹出第一个元素并全部左移









#ifdef __cplusplus
}
#endif

#endif
