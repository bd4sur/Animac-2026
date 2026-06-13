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
// 内部辅助函数
// ===============================================================================

// 在闭包中线性查找指定 varid 与 type 的绑定。
// 返回指向匹配 binding 的指针；未找到返回 NULL。
static inline am_binding_t *am_closure_find(am_obj_closure_t *closure, am_varid_t varid, int32_t type) {
    for (size_t i = 0; i < closure->length; i++) {
        if (closure->bindings[i].varid == varid && closure->bindings[i].type == type) {
            return &closure->bindings[i];
        }
    }
    return NULL;
}

// 仅按 varid 查找（不区分绑定类型），用于脏标记查询。
// 返回指向匹配 binding 的指针；未找到返回 NULL。
static inline am_binding_t *am_closure_find_by_varid(am_obj_closure_t *closure, am_varid_t varid) {
    for (size_t i = 0; i < closure->length; i++) {
        if (closure->bindings[i].varid == varid) {
            return &closure->bindings[i];
        }
    }
    return NULL;
}

// 将闭包扩容到新容量，返回新的闭包对象指针；失败返回 NULL。
// 原闭包对象会被释放，调用者必须使用返回的新指针。
static inline am_obj_closure_t *am_closure_resize(am_allocator_t *alloc, am_obj_closure_t *closure, size_t new_capacity) {
    size_t total_size = sizeof(am_obj_closure_t) + new_capacity * sizeof(am_binding_t);
    am_obj_closure_t *new_closure = (am_obj_closure_t *)am_malloc(alloc, total_size);
    if (!new_closure) return NULL;

    // 拷贝头部与已有 binding（拷贝长度按原 length，而非原 capacity）
    memcpy(new_closure, closure, sizeof(am_obj_closure_t));
    if (closure->length > 0) {
        memcpy(new_closure->bindings, closure->bindings, closure->length * sizeof(am_binding_t));
    }
    new_closure->capacity = new_capacity;

    am_free(alloc, closure);
    return new_closure;
}

// 若空间不足则扩容。绝大多数情况下不会触发实际分配。
// 返回原指针或新指针；失败返回 NULL。
static inline am_obj_closure_t *am_closure_grow_if_needed(am_allocator_t *alloc, am_obj_closure_t *closure) {
    if (closure->length < closure->capacity) return closure;

    size_t new_capacity = closure->capacity * 2;
    if (new_capacity < 16) new_capacity = 16;
    return am_closure_resize(alloc, closure, new_capacity);
}

// ===============================================================================
// 构造函数
// ===============================================================================

// 创建闭包。capacity 为 0 时默认使用 16。
static inline am_obj_closure_t *am_closure_create(am_allocator_t *alloc, am_iaddr_t iaddr, am_handle_t parent, size_t capacity) {
    if (capacity == 0) capacity = 16;

    size_t total_size = sizeof(am_obj_closure_t) + capacity * sizeof(am_binding_t);
    am_obj_closure_t *closure = (am_obj_closure_t *)am_malloc(alloc, total_size);
    if (!closure) return NULL;

    memset(closure, 0, total_size);

    closure->base.type = AM_OBJECT_TYPE_CLOSURE;
    closure->iaddr = iaddr;
    closure->parent = parent;
    closure->length = 0;
    closure->capacity = capacity;

    return closure;
}

// ===============================================================================
// 析构
// ===============================================================================

// 销毁闭包对象。binding 中的 value 按引用处理，不由闭包释放。
static inline int32_t am_closure_destroy(am_allocator_t *alloc, am_obj_closure_t *closure) {
    am_free(alloc, closure);
    return 0;
}

// ===============================================================================
// 拷贝
// ===============================================================================

// 深拷贝（头部与所有 binding）。value 按位拷贝（与 TS Copy 语义一致，不递归释放对象）。
static inline am_obj_closure_t *am_closure_copy(am_allocator_t *alloc, am_obj_closure_t *closure) {
    am_obj_closure_t *copy = am_closure_create(alloc, closure->iaddr, closure->parent, closure->capacity);
    if (!copy) return NULL;

    copy->length = closure->length;
    if (closure->length > 0) {
        memcpy(copy->bindings, closure->bindings, closure->length * sizeof(am_binding_t));
    }
    return copy;
}

// ===============================================================================
// 约束变量操作
// ===============================================================================

// 初始化约束变量（不加脏标记）。若已存在则更新 value 并清除脏标记。
// 如涉及扩容，返回新闭包对象指针；否则返回原指针。失败返回 NULL。
static inline am_obj_closure_t *am_closure_init_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value) {
    am_binding_t *binding = am_closure_find(closure, variable, AM_BINDING_BOUND);
    if (binding) {
        binding->value = value;
        binding->dirty_flag = 0;
        return closure;
    }

    closure = am_closure_grow_if_needed(alloc, closure);
    if (!closure) return NULL;

    closure->bindings[closure->length].varid = variable;
    closure->bindings[closure->length].type = AM_BINDING_BOUND;
    closure->bindings[closure->length].dirty_flag = 0;
    closure->bindings[closure->length].value = value;
    closure->length++;
    return closure;
}

// 设置约束变量（加脏标记，仅用于 set 指令）。若不存在则插入。
// 返回新指针或原指针；失败返回 NULL。
static inline am_obj_closure_t *am_closure_set_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value) {
    am_binding_t *binding = am_closure_find(closure, variable, AM_BINDING_BOUND);
    if (binding) {
        binding->value = value;
        binding->dirty_flag = 1;
        return closure;
    }

    closure = am_closure_grow_if_needed(alloc, closure);
    if (!closure) return NULL;

    closure->bindings[closure->length].varid = variable;
    closure->bindings[closure->length].type = AM_BINDING_BOUND;
    closure->bindings[closure->length].dirty_flag = 1;
    closure->bindings[closure->length].value = value;
    closure->length++;
    return closure;
}

// 获取约束变量。未找到返回 AM_VALUE_UNDEFINED。
static inline am_value_t am_closure_get_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable) {
    (void)alloc;
    am_binding_t *binding = am_closure_find(closure, variable, AM_BINDING_BOUND);
    return binding ? binding->value : AM_VALUE_UNDEFINED;
}

// ===============================================================================
// 自由变量操作
// ===============================================================================

// 初始化自由变量（不加脏标记）。若已存在则更新 value 并清除脏标记。
static inline am_obj_closure_t *am_closure_init_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value) {
    am_binding_t *binding = am_closure_find(closure, variable, AM_BINDING_FREE);
    if (binding) {
        binding->value = value;
        binding->dirty_flag = 0;
        return closure;
    }

    closure = am_closure_grow_if_needed(alloc, closure);
    if (!closure) return NULL;

    closure->bindings[closure->length].varid = variable;
    closure->bindings[closure->length].type = AM_BINDING_FREE;
    closure->bindings[closure->length].dirty_flag = 0;
    closure->bindings[closure->length].value = value;
    closure->length++;
    return closure;
}

// 设置自由变量（加脏标记，仅用于 set 指令）。若不存在则插入。
static inline am_obj_closure_t *am_closure_set_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value) {
    am_binding_t *binding = am_closure_find(closure, variable, AM_BINDING_FREE);
    if (binding) {
        binding->value = value;
        binding->dirty_flag = 1;
        return closure;
    }

    closure = am_closure_grow_if_needed(alloc, closure);
    if (!closure) return NULL;

    closure->bindings[closure->length].varid = variable;
    closure->bindings[closure->length].type = AM_BINDING_FREE;
    closure->bindings[closure->length].dirty_flag = 1;
    closure->bindings[closure->length].value = value;
    closure->length++;
    return closure;
}

// 获取自由变量。未找到返回 AM_VALUE_UNDEFINED。
static inline am_value_t am_closure_get_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable) {
    (void)alloc;
    am_binding_t *binding = am_closure_find(closure, variable, AM_BINDING_FREE);
    return binding ? binding->value : AM_VALUE_UNDEFINED;
}

// ===============================================================================
// 查询
// ===============================================================================

// 判断变量是否为脏。未找到返回 0（false）。
static inline int32_t am_closure_is_dirty_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable) {
    (void)alloc;
    am_binding_t *binding = am_closure_find_by_varid(closure, variable);
    return binding ? binding->dirty_flag : 0;
}

// 是否存在约束变量绑定。
static inline int32_t am_closure_has_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable) {
    (void)alloc;
    return am_closure_find(closure, variable, AM_BINDING_BOUND) ? 1 : 0;
}

// 是否存在自由变量绑定。
static inline int32_t am_closure_has_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable) {
    (void)alloc;
    return am_closure_find(closure, variable, AM_BINDING_FREE) ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif
