#ifndef __AM_CLOSURE_H__
#define __AM_CLOSURE_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////
// 语言数据对象：闭包
///////////////////////////////////////////

// 变量绑定类型（约束绑定、自由绑定）
#define AM_BINDING_BOUND (1)
#define AM_BINDING_FREE  (2)

// 闭包中的变量绑定字段
typedef struct am_binding_t {
    am_varid_t  varid;
    int32_t     type;       // 变量绑定类型（约束绑定、自由绑定）
    int32_t     dirty_flag; // 脏标记
    am_value_t  value;
} am_binding_t;

// 闭包堆对象（变量绑定和元数据用线性表（柔性数组）实现）
typedef struct am_obj_closure_t {
    am_object_t base;

    am_iaddr_t   iaddr;      // 所在call指令的iaddr
    am_handle_t  parent;     // 亲闭包把柄
    size_t       length;     // 指的是bindings的元素个数
    size_t       capacity;   // bindings数组的容量（涉及动态扩容和重新分配）
    am_binding_t bindings[]; // 柔性数组，按顺序逐个插入
} am_obj_closure_t;


// ===============================================================================
// 构造函数
// ===============================================================================

// 创建闭包。capacity 为 0 时默认使用 16。
am_obj_closure_t *am_closure_create(am_allocator_t *alloc, am_iaddr_t iaddr, am_handle_t parent, size_t capacity);


// ===============================================================================
// 析构
// ===============================================================================

// 销毁闭包对象。binding 中的 value 按引用处理，不由闭包释放。
int32_t am_closure_destroy(am_allocator_t *alloc, am_obj_closure_t *closure);


// ===============================================================================
// 拷贝
// ===============================================================================

// 深拷贝（头部与所有 binding）。value 按位拷贝（与 TS Copy 语义一致，不递归释放对象）。
am_obj_closure_t *am_closure_copy(am_allocator_t *alloc, am_obj_closure_t *closure);


// ===============================================================================
// 对象二进制转储 TODO
// ===============================================================================

// 将对象的二进制内存布局从alloc管理的内存中倒出来，返回一个系统malloc的二进制序列，以及序列长度
//   注意：压缩对象，将capacity压缩到跟length一致，删除多余分配的空闲部分
uint8_t *am_closure_dump(am_allocator_t *alloc, am_obj_closure_t *closure, size_t *size);


// ===============================================================================
// 约束变量操作
// ===============================================================================

// 初始化约束变量（不加脏标记）。若已存在则更新 value 并清除脏标记。
// 如涉及扩容，返回新闭包对象指针；否则返回原指针。失败返回 NULL。
am_obj_closure_t *am_closure_init_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value);

// 设置约束变量（加脏标记，仅用于 set 指令）。若不存在则插入。
// 返回新指针或原指针；失败返回 NULL。
am_obj_closure_t *am_closure_set_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value);

// 获取约束变量。未找到返回 AM_VALUE_UNDEFINED。
am_value_t am_closure_get_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable);


// ===============================================================================
// 自由变量操作
// ===============================================================================

// 初始化自由变量（不加脏标记）。若已存在则更新 value 并清除脏标记。
am_obj_closure_t *am_closure_init_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value);

// 设置自由变量（加脏标记，仅用于 set 指令）。若不存在则插入。
am_obj_closure_t *am_closure_set_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value);

// 获取自由变量。未找到返回 AM_VALUE_UNDEFINED。
am_value_t am_closure_get_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable);


// ===============================================================================
// 查询
// ===============================================================================

// 判断变量是否为脏。为脏返回 0，未找到或不为脏返回 -1。
int32_t am_closure_is_dirty_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable);

// 是否存在约束变量绑定。存在返回 0，不存在返回 -1。
int32_t am_closure_has_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable);

// 是否存在自由变量绑定。存在返回 0，不存在返回 -1。
int32_t am_closure_has_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable);


#ifdef __cplusplus
}
#endif

#endif
