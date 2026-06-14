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
// 线性表及其子类型
///////////////////////////////////////////

// List子类型，决定了编译器和虚拟机如何解释List对象，这是Homoiconicity的基石
#define AM_LIST_TYPE_DEFAULT     (0) // 一般的运行时对象
#define AM_LIST_TYPE_LAMBDA      (1) // TODO lambda的对象布局不做特殊处理，以实现Homoiconicity
#define AM_LIST_TYPE_APPLICATION (2) // 实际等同于AM_SLIST_TYPE_DEFAULT
#define AM_LIST_TYPE_QUOTE       (3)
#define AM_LIST_TYPE_QUASIQUOTE  (4)
#define AM_LIST_TYPE_UNQUOTE     (5)

// 通用线性表（动态扩容）：同时作为基础数据结构和语言数据对象
// NOTE 说明：am_list_t虽然是基础数据结构，但实质上可作为对象语言的数据对象。详见am_map_t的说明。
typedef struct am_list_t {
    am_object_t base;

    size_t      capacity;   // children容量
    size_t      length;     // children元素个数（最后一个元素的下标+1）
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




///////////////////////////////////////////
// Lambda表相关函数
///////////////////////////////////////////


// Lambda表结构说明：Lambda表采用形参列表扁平化存储的设计，具体如下。
// children = ['lambda , n_param , param0 , ... , param(n-1) , body0 , ...]
// 
// - length = children项数，等于lambda关键字1项+形参数量字段1项+形参数量+函数体项数
// - children[0] = AM_VALUE_KW_lambda
// - children[1] = am_value_t(满足am_value_is_uint) 引数（形参）数量，记为n
// - children[2 ~ (2+n)] = n个形参的am_varid_t，且都必须为am_varid_n类型
// - children[(2+n) ~ length] = lambda函数体各项的am_value_t
// 
// 例如：(lambda (x y) 666) 对应列表对象的children为：['lambda , 2 , x_varid , y_varid , 666]，因而形参数为2，函数体项数=length-形参数-2=1



// 向Lambda表 增加一个形式参数
// 返回：列表对象指针。若执行成功，则返回原列表指针（无扩容）或新列表指针（有扩容）。若执行失败，则返回NULL作为标记。
// 参数：am_value_t param 必须满足am_value_is_varid(param)
am_list_t *am_list_lambda_add_parameter(am_allocator_t *alloc, am_list_t *lst, am_value_t param);


// 向Lambda表 增加一个函数体
// 返回：列表对象指针。若执行成功，则返回原列表指针（无扩容）或新列表指针（有扩容）。若执行失败，则返回NULL作为标记。
// 参数：am_value_t body
am_list_t *am_list_lambda_add_body(am_allocator_t *alloc, am_list_t *lst, am_value_t body);


// 从Lambda表中 获取函数体数量：根据length、children[1]值和列表结构，计算出函数体数量，并将其值转为size_t
size_t am_list_lambda_get_body_number(am_allocator_t *alloc, am_list_t *lst);


// 从Lambda表中 获取函数体列表和数量
// 返回：函数体am_value_t的数组指针（调用者负责free）
// 参数：am_value_t body
am_value_t *am_list_lambda_get_bodies(am_allocator_t *alloc, am_list_t *lst, size_t *n_body);


// 对Lambda表 重新设置（覆盖）所有的函数体
// 返回：列表对象指针。若执行成功，则返回原列表指针（无扩容）或新列表指针（有扩容）。若执行失败，则返回NULL作为标记。
// 说明：将函数体列表整体替换掉，如果原来的函数体较多，则将多余的旧函数体全部清空，同时保证length字段正确
am_list_t *am_list_lambda_set_bodies(am_allocator_t *alloc, am_list_t *lst, am_value_t *bodies, size_t *n_body);


#ifdef __cplusplus
}
#endif

#endif
