#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "process.h"


// ===============================================================================
// 内部辅助函数
// ===============================================================================

// 将proc->heap中所有对象标记为静态对象（通常用于从模块加载的初始AST数据）
static void set_all_heap_objects_static(am_handle_t handle, am_value_t value, void *user_data) {
    (void)handle;
    (void)user_data;
    if (am_value_is_ptr(value)) {
        am_object_t *obj = am_value_to_ptr(value);
        if (obj != NULL) {
            am_object_set_static(obj, 0);
        }
    }
}


// 判断一个am_value_t是否为指向堆对象的把柄（handle）
static inline bool is_handle_value(am_value_t v) {
    return am_value_is_handle(v);
}


// GC根收集辅助函数：分析一组运行时环境（当前闭包、opstack、fstack）中的GC根
static int32_t gc_root_helper(
    am_process_t *proc, am_list_t *gcroots,
    am_handle_t current_closure_handle,
    am_value_t *opstack, size_t opstack_length,
    am_value_t *fstack, size_t fstack_length
) {
    if (!proc || !gcroots) return -1;

    // 加入当前闭包handle
    am_list_t *lst = am_list_push(proc->vm_alloc, gcroots, am_make_value_of_handle(current_closure_handle));
    if (!lst) return -1;
    gcroots = lst;

    // 加入当前闭包内的变量绑定（约束变量和自由变量）
    am_obj_closure_t *current_closure_obj = am_process_get_closure(proc, current_closure_handle);
    if (current_closure_obj) {
        for (size_t i = 0; i < current_closure_obj->length; i++) {
            am_value_t value = current_closure_obj->bindings[i].value;
            if (is_handle_value(value)) {
                lst = am_list_push(proc->vm_alloc, gcroots, value);
                if (!lst) return -1;
                gcroots = lst;
            }
        }
    }

    // 加入操作数栈内的把柄
    for (size_t i = 0; i < opstack_length; i++) {
        am_value_t v = opstack[i];
        if (is_handle_value(v)) {
            lst = am_list_push(proc->vm_alloc, gcroots, v);
            if (!lst) return -1;
            gcroots = lst;
        }
    }

    // 加入函数调用栈中每个栈帧对应的闭包把柄，以及这些闭包内的变量绑定
    // fstack成对存储：closure_handle_value, return_target_iaddr_value
    for (size_t i = 0; i + 1 < fstack_length; i += 2) {
        am_value_t closure_handle_value = fstack[i];
        am_value_t return_target_iaddr_value = fstack[i + 1];
        (void)return_target_iaddr_value;

        if (!am_value_is_handle(closure_handle_value)) {
            fprintf(stderr, "[gc_root_helper] 预期闭包handle，实际非handle\n");
            return -1;
        }

        am_handle_t closure_handle = am_value_to_handle(closure_handle_value);
        if (closure_handle == AM_HANDLE_NULL) continue;
        am_obj_closure_t *closure_obj = am_process_get_closure(proc, closure_handle);
        if (!closure_obj) {
            fprintf(stderr, "[gc_root_helper] 无法获取闭包对象 %zu\n", closure_handle);
            continue;
        }
        if (closure_obj->base.type != AM_OBJECT_TYPE_CLOSURE) {
            fprintf(stderr, "[gc_root_helper] 预期闭包，实际非闭包\n");
            return -1;
        }

        // 将栈帧的闭包handle加入GC根
        lst = am_list_push(proc->vm_alloc, gcroots, closure_handle_value);
        if (!lst) return -1;
        gcroots = lst;

        // 将该闭包内的变量绑定中的handle加入GC根
        for (size_t j = 0; j < closure_obj->length; j++) {
            am_value_t value = closure_obj->bindings[j].value;
            if (is_handle_value(value)) {
                lst = am_list_push(proc->vm_alloc, gcroots, value);
                if (!lst) return -1;
                gcroots = lst;
            }
        }
    }

    return 0;
}


// ===============================================================================
// 生命周期
// ===============================================================================

// 功能说明：从模块构造并初始化一个新的进程数据结构
// 实现说明：成功返回新进程对象指针；失败返回NULL
am_process_t *am_process_load_from_module(am_allocator_t *vm_alloc, am_allocator_t *heap_alloc, am_module_t *mod) {
    if (!vm_alloc || !heap_alloc || !mod || !mod->ast || !mod->ilcode) {
        return NULL;
    }

    am_process_t *proc = (am_process_t *)am_calloc(vm_alloc, sizeof(am_process_t));
    if (!proc) return NULL;

    proc->base.type = AM_OBJECT_TYPE_BASE;
    proc->vm_alloc = vm_alloc;
    proc->heap_alloc = heap_alloc;
    proc->pid = 0;
    proc->parent_pid = 0;
    proc->state = AM_PROCESS_STATE_READY;
    proc->PC = 0;
    proc->current_closure_handle = AM_HANDLE_NULL;

    // 复制中间语言代码到进程
    proc->ilcode_length = mod->ilcode_length;
    proc->ilcode = (am_instruction_t *)am_malloc(vm_alloc, mod->ilcode_length * sizeof(am_instruction_t));
    if (!proc->ilcode) {
        am_free(vm_alloc, proc);
        return NULL;
    }
    memcpy(proc->ilcode, mod->ilcode, mod->ilcode_length * sizeof(am_instruction_t));

    // 将mod->ast->nodes深拷贝到proc->heap
    // 先通过deep_dump计算大小并序列化，再用deep_load到进程堆
    size_t dump_size = am_heap_deep_dump(mod->ast->alloc, mod->ast->nodes, NULL, 0);
    if (dump_size == SIZE_MAX) {
        am_free(vm_alloc, proc->ilcode);
        am_free(vm_alloc, proc);
        return NULL;
    }

    uint8_t *buffer = (uint8_t *)am_malloc(vm_alloc, dump_size);
    if (!buffer) {
        am_free(vm_alloc, proc->ilcode);
        am_free(vm_alloc, proc);
        return NULL;
    }
    memset(buffer, 0, dump_size);

    size_t written = am_heap_deep_dump(mod->ast->alloc, mod->ast->nodes, buffer, 0);
    if (written != dump_size) {
        am_free(vm_alloc, buffer);
        am_free(vm_alloc, proc->ilcode);
        am_free(vm_alloc, proc);
        return NULL;
    }

    proc->heap = am_heap_deep_load(heap_alloc, buffer, 0);
    am_free(vm_alloc, buffer);
    if (!proc->heap) {
        am_free(vm_alloc, proc->ilcode);
        am_free(vm_alloc, proc);
        return NULL;
    }

    // 将拷贝进来的AST节点全部标记为静态对象，避免被GC回收
    am_heap_iter(heap_alloc, proc->heap, set_all_heap_objects_static, NULL);

    // 拷贝符号表
    proc->var_vocab = am_vocab_copy(proc->vm_alloc, mod->ast->var_vocab);
    proc->symbol_vocab = am_vocab_copy(proc->vm_alloc, mod->ast->symbol_vocab);

    // 分配操作数栈
    proc->opstack_capacity = (size_t)(mod->opstack_depth > 0 ? mod->opstack_depth : 1024);
    proc->opstack = (am_value_t *)am_calloc(vm_alloc, proc->opstack_capacity * sizeof(am_value_t));
    if (!proc->opstack) {
        am_heap_destroy(heap_alloc, proc->heap);
        am_free(vm_alloc, proc->ilcode);
        am_free(vm_alloc, proc);
        return NULL;
    }
    proc->opstack_top = proc->opstack;

    // 分配函数调用栈
    proc->fstack_capacity = 3000;
    proc->fstack = (am_value_t *)am_calloc(vm_alloc, proc->fstack_capacity * sizeof(am_value_t));
    if (!proc->fstack) {
        am_free(vm_alloc, proc->opstack);
        am_heap_destroy(heap_alloc, proc->heap);
        am_free(vm_alloc, proc->ilcode);
        am_free(vm_alloc, proc);
        return NULL;
    }
    proc->fstack_top = proc->fstack;

    return proc;
}


// 功能说明：销毁进程数据结构，释放其占用的全部资源
// 实现说明：成功返回0，失败返回-1
int32_t am_process_destroy(am_process_t *proc) {
    if (!proc) return 0;

    if (proc->ilcode) {
        am_free(proc->vm_alloc, proc->ilcode);
        proc->ilcode = NULL;
    }
    if (proc->opstack) {
        am_free(proc->vm_alloc, proc->opstack);
        proc->opstack = NULL;
        proc->opstack_top = NULL;
    }
    if (proc->fstack) {
        am_free(proc->vm_alloc, proc->fstack);
        proc->fstack = NULL;
        proc->fstack_top = NULL;
    }
    if (proc->heap) {
        am_heap_destroy(proc->heap_alloc, proc->heap);
        proc->heap = NULL;
    }

    am_free(proc->vm_alloc, proc);
    return 0;
}


// ===============================================================================
// 操作数栈操作
// ===============================================================================

// 功能说明：向操作数栈中压入值。成功返回0，失败返回-1
int32_t am_process_push_operand(am_process_t *proc, am_value_t v) {
    if (!proc || !proc->opstack || !proc->opstack_top) return -1;
    size_t used = (size_t)(proc->opstack_top - proc->opstack);
    if (used >= proc->opstack_capacity) return -1;
    *proc->opstack_top++ = v;
    return 0;
}


// 功能说明：从操作数栈中弹出一个值。成功返回弹出值，失败返回UINTPTR_MAX
am_value_t am_process_pop_operand(am_process_t *proc) {
    if (!proc || !proc->opstack || !proc->opstack_top) return (am_value_t)UINTPTR_MAX;
    if (proc->opstack_top <= proc->opstack) return (am_value_t)UINTPTR_MAX;
    return *--proc->opstack_top;
}


// 功能说明：根据栈顶指针计算opstack中有多少个am_value_t。成功返回长度值，失败返回SIZE_MAX
size_t am_process_length_of_opstack(am_process_t *proc) {
    if (!proc || !proc->opstack || !proc->opstack_top) return SIZE_MAX;
    return (size_t)(proc->opstack_top - proc->opstack);
}


// ===============================================================================
// 函数调用栈操作
// ===============================================================================

// 功能说明：向fstack中压入栈帧（两个值）。成功返回0，失败返回-1
int32_t am_process_push_stack_frame(am_process_t *proc, am_value_t closure_handle_value, am_value_t return_target_iaddr_value) {
    if (!proc || !proc->fstack || !proc->fstack_top) return -1;
    if (!am_value_is_handle(closure_handle_value)) return -1;
    if (!am_value_is_iaddr(return_target_iaddr_value)) return -1;

    size_t used = (size_t)(proc->fstack_top - proc->fstack);
    if (used + 2 > proc->fstack_capacity) return -1;

    *proc->fstack_top++ = closure_handle_value;
    *proc->fstack_top++ = return_target_iaddr_value;
    return 0;
}


// 功能说明：从fstack中弹出栈帧的两个值，通过两个指针传出。成功返回0，失败返回-1
int32_t am_process_pop_stack_frame(am_process_t *proc, am_value_t *closure_handle_value, am_value_t *return_target_iaddr_value) {
    if (!proc || !proc->fstack || !proc->fstack_top || !closure_handle_value || !return_target_iaddr_value) return -1;
    if (proc->fstack_top - proc->fstack < 2) return -1;

    *return_target_iaddr_value = *--proc->fstack_top;
    *closure_handle_value = *--proc->fstack_top;
    return 0;
}


// 功能说明：根据栈顶指针计算fstack中有多少个am_value_t（因为是成对push/pop，所以正常情况下必为偶数）。成功返回长度值，失败返回SIZE_MAX
size_t am_process_length_of_fstack(am_process_t *proc) {
    if (!proc || !proc->fstack || !proc->fstack_top) return SIZE_MAX;
    return (size_t)(proc->fstack_top - proc->fstack);
}


// ===============================================================================
// 闭包操作
// ===============================================================================

// 功能说明：新建闭包并返回其handle。成功返回handle，失败返回AM_HANDLE_NULL
am_handle_t am_process_make_closure(am_process_t *proc, am_iaddr_t iaddr, am_handle_t parent) {
    if (!proc || !proc->heap || !proc->heap_alloc) return AM_HANDLE_NULL;

    // 首先在proc->heap中申请一个新的handle
    am_handle_t hd = am_heap_alloc_handle(proc->heap_alloc, proc->heap);
    if (hd == AM_HANDLE_NULL) return AM_HANDLE_NULL;

    // 新建闭包对象
    am_obj_closure_t *closure_obj = am_closure_create(proc->heap_alloc, iaddr, parent, 16);
    if (!closure_obj) {
        am_heap_free_handle(proc->heap_alloc, proc->heap, hd);
        return AM_HANDLE_NULL;
    }

    // 将闭包对象的指针绑定到hd上
    am_value_t closure_value = am_make_value_of_ptr((am_object_t *)closure_obj);
    if (am_heap_set(proc->heap_alloc, proc->heap, hd, closure_value) != 0) {
        am_closure_destroy(proc->heap_alloc, closure_obj);
        am_heap_free_handle(proc->heap_alloc, proc->heap, hd);
        return AM_HANDLE_NULL;
    }

    return hd;
}


// 功能说明：根据闭包handle获取闭包对象。成功返回指针，失败返回NULL
am_obj_closure_t *am_process_get_closure(am_process_t *proc, am_handle_t hd) {
    if (!proc || !proc->heap) return NULL;

    am_value_t v = am_heap_get(proc->heap_alloc, proc->heap, hd);
    if (!am_value_is_ptr(v)) return NULL;

    am_object_t *obj = am_value_to_ptr(v);
    if (!obj || obj->type != AM_OBJECT_TYPE_CLOSURE) return NULL;

    return (am_obj_closure_t *)obj;
}


// 功能说明：获取进程的当前闭包对象。成功返回指针，失败返回NULL
am_obj_closure_t *am_process_get_current_closure(am_process_t *proc) {
    if (!proc) return NULL;
    return am_process_get_closure(proc, proc->current_closure_handle);
}


// 功能说明：变量解引用。成功返回TPV，失败返回UINTPTR_MAX
// 设计说明：先在当前闭包查找约束变量；若不存在，则沿闭包链查找约束变量定义位置，
//          根据脏标记决定使用定义位置的约束变量值，还是使用当前闭包中的自由变量值。
am_value_t am_process_dereference(am_process_t *proc, am_varid_t varid) {
    if (!proc) return (am_value_t)UINTPTR_MAX;

    am_obj_closure_t *current_closure_obj = am_process_get_current_closure(proc);
    if (!current_closure_obj) return (am_value_t)UINTPTR_MAX;

    // 查找当前闭包的约束变量
    if (am_closure_has_bound_var(proc->heap_alloc, current_closure_obj, varid) == 0) {
        return am_closure_get_bound_var(proc->heap_alloc, current_closure_obj, varid);
    }

    // 查找当前闭包的自由变量（如果存在）对应的词法定义环境
    am_handle_t closure_handle = proc->current_closure_handle;
    while (closure_handle != AM_HANDLE_NULL) {
        am_obj_closure_t *closure_obj = am_process_get_closure(proc, closure_handle);
        if (!closure_obj) break;

        if (am_closure_has_bound_var(proc->heap_alloc, closure_obj, varid) == 0) {
            // 找到约束变量定义位置
            if (am_closure_is_dirty_var(proc->heap_alloc, closure_obj, varid) == 0) {
                // 脏标记为真：使用约束变量定义位置的新值
                return am_closure_get_bound_var(proc->heap_alloc, closure_obj, varid);
            }
            else {
                // 脏标记为假：使用当前闭包中的自由变量值
                return am_closure_get_free_var(proc->heap_alloc, current_closure_obj, varid);
            }
        }

        closure_handle = closure_obj->parent;
    }

    // 未找到变量定义
    return (am_value_t)UINTPTR_MAX;
}


// ===============================================================================
// 程序流程控制
// ===============================================================================

// 功能说明：获取当前指令，并取出opcode和operand。成功返回0，失败返回-1
int32_t am_process_current_instruction(am_process_t *proc, uint32_t *opcode, am_value_t *operand) {
    if (!proc || !opcode || !operand) return -1;
    if (!proc->ilcode || proc->PC >= proc->ilcode_length) return -1;

    *opcode = proc->ilcode[proc->PC].opcode;
    *operand = proc->ilcode[proc->PC].operand;
    return 0;
}


// 功能说明：前进一步（PC加1）
void am_process_step(am_process_t *proc) {
    if (!proc) return;
    proc->PC++;
}


// 功能说明：无条件跳转（PC置数iaddr）
void am_process_goto(am_process_t *proc, am_iaddr_t iaddr) {
    if (!proc) return;
    proc->PC = iaddr;
}


// 功能说明：设置进程状态
void am_process_set_state(am_process_t *proc, int32_t s) {
    if (!proc) return;
    proc->state = s;
}


// ===============================================================================
// 计算续体（continuation）的捕获和恢复
// ===============================================================================

// 功能说明：捕获当前续体，保存为堆对象，并返回其handle。成功返回handle，失败返回AM_HANDLE_NULL
am_handle_t am_process_capture_continuation(am_process_t *proc, am_iaddr_t cont_return_target_iaddr) {
    if (!proc || !proc->heap || !proc->heap_alloc) return AM_HANDLE_NULL;

    size_t opstack_length = am_process_length_of_opstack(proc);
    size_t fstack_length = am_process_length_of_fstack(proc);
    if (opstack_length == SIZE_MAX || fstack_length == SIZE_MAX) return AM_HANDLE_NULL;

    // 创建续体对象，深拷贝当前opstack和fstack
    am_continuation_t *cont = am_continuation_create(
        proc->heap_alloc,
        cont_return_target_iaddr,
        proc->current_closure_handle,
        proc->opstack, opstack_length,
        proc->fstack, fstack_length
    );
    if (!cont) return AM_HANDLE_NULL;

    // 在堆中分配handle并绑定续体对象
    am_handle_t hd = am_heap_alloc_handle(proc->heap_alloc, proc->heap);
    if (hd == AM_HANDLE_NULL) {
        am_continuation_destroy(proc->heap_alloc, cont);
        return AM_HANDLE_NULL;
    }

    am_value_t cont_value = am_make_value_of_ptr((am_object_t *)cont);
    if (am_heap_set(proc->heap_alloc, proc->heap, hd, cont_value) != 0) {
        am_continuation_destroy(proc->heap_alloc, cont);
        am_heap_free_handle(proc->heap_alloc, proc->heap, hd);
        return AM_HANDLE_NULL;
    }

    return hd;
}


// 功能说明：恢复指定的计算续体到当前进程。成功返回其返回目标位置的iaddr，失败返回SIZE_MAX
am_iaddr_t am_process_load_continuation(am_process_t *proc, am_handle_t hd) {
    if (!proc || !proc->heap) return SIZE_MAX;

    am_value_t v = am_heap_get(proc->heap_alloc, proc->heap, hd);
    if (!am_value_is_ptr(v)) return SIZE_MAX;

    am_object_t *obj = am_value_to_ptr(v);
    if (!obj || obj->type != AM_OBJECT_TYPE_CONTINUATION) return SIZE_MAX;

    am_continuation_t *cont = (am_continuation_t *)obj;

    // 获取续体中保存的opstack和fstack副本
    size_t cont_opstack_length = 0;
    size_t cont_fstack_length = 0;
    am_value_t *cont_opstack = am_continuation_get_opstack(proc->vm_alloc, cont, &cont_opstack_length);
    am_value_t *cont_fstack = am_continuation_get_fstack(proc->vm_alloc, cont, &cont_fstack_length);

    if (!cont_opstack || !cont_fstack) {
        if (cont_opstack) am_free(proc->vm_alloc, cont_opstack);
        if (cont_fstack) am_free(proc->vm_alloc, cont_fstack);
        return SIZE_MAX;
    }

    // 检查容量是否足够
    if (cont_opstack_length > proc->opstack_capacity || cont_fstack_length > proc->fstack_capacity) {
        am_free(proc->vm_alloc, cont_opstack);
        am_free(proc->vm_alloc, cont_fstack);
        return SIZE_MAX;
    }

    // 恢复运行时状态
    if (cont_opstack_length > 0) {
        memcpy(proc->opstack, cont_opstack, cont_opstack_length * sizeof(am_value_t));
    }
    proc->opstack_top = proc->opstack + cont_opstack_length;

    if (cont_fstack_length > 0) {
        memcpy(proc->fstack, cont_fstack, cont_fstack_length * sizeof(am_value_t));
    }
    proc->fstack_top = proc->fstack + cont_fstack_length;

    proc->current_closure_handle = cont->current_closure_handle;

    am_free(proc->vm_alloc, cont_opstack);
    am_free(proc->vm_alloc, cont_fstack);

    return cont->cont_return_target;
}


// ===============================================================================
// 垃圾回收算法（分进程标记-清除）
// ===============================================================================

// 功能说明：从当前进程和续体环境中收集GC根。成功返回0，失败返回-1
// 设计说明：可达性分析的根（GC根）有：当前闭包本身、当前闭包和函数调用栈对应闭包内的变量绑定、操作数栈内的把柄、函数调用栈内所有栈帧对应的闭包把柄、所有continuation中保留的上面的各项
// 实现说明：gcroots是收集到的GC根的TPV的列表，由外部分配和释放。
int32_t am_process_gc_root(am_process_t *proc, am_list_t *gcroots) {
    if (!proc || !gcroots || !proc->heap) return -1;

    // 分析当前进程中的GC根
    size_t opstack_length = am_process_length_of_opstack(proc);
    size_t fstack_length = am_process_length_of_fstack(proc);
    if (opstack_length == SIZE_MAX || fstack_length == SIZE_MAX) return -1;

    if (gc_root_helper(proc, gcroots, proc->current_closure_handle,
                       proc->opstack, opstack_length,
                       proc->fstack, fstack_length) != 0) {
        return -1;
    }

    // 分析所有已保存的续体环境中的GC根
    // 遍历堆中所有对象，找到continuation对象
    size_t heap_count = am_map_length(proc->heap_alloc, proc->heap->table);
    am_value_t *keys = am_map_keys(proc->heap_alloc, proc->heap->table);
    if (!keys && heap_count > 0) return -1;

    int32_t ret = 0;
    for (size_t i = 0; i < heap_count; i++) {
        am_handle_t hd = am_value_to_handle(keys[i]);
        am_value_t value = am_heap_get(proc->heap_alloc, proc->heap, hd);
        if (!am_value_is_ptr(value)) continue;

        am_object_t *obj = am_value_to_ptr(value);
        if (!obj || obj->type != AM_OBJECT_TYPE_CONTINUATION) continue;

        am_continuation_t *cont = (am_continuation_t *)obj;

        // 将续体内部环境加入GC根
        size_t cont_opstack_length = 0;
        size_t cont_fstack_length = 0;
        am_value_t *cont_opstack = am_continuation_get_opstack(proc->vm_alloc, cont, &cont_opstack_length);
        am_value_t *cont_fstack = am_continuation_get_fstack(proc->vm_alloc, cont, &cont_fstack_length);

        if (!cont_opstack || !cont_fstack) {
            if (cont_opstack) am_free(proc->vm_alloc, cont_opstack);
            if (cont_fstack) am_free(proc->vm_alloc, cont_fstack);
            ret = -1;
            break;
        }

        if (gc_root_helper(proc, gcroots, cont->current_closure_handle,
                           cont_opstack, cont_opstack_length,
                           cont_fstack, cont_fstack_length) != 0) {
            am_free(proc->vm_alloc, cont_opstack);
            am_free(proc->vm_alloc, cont_fstack);
            ret = -1;
            break;
        }

        am_free(proc->vm_alloc, cont_opstack);
        am_free(proc->vm_alloc, cont_fstack);
    }

    free(keys);
    return ret;
}


// 功能说明：从GC根开始，递归标记存活对象。成功返回0，失败返回-1（或更小的负数）
int32_t am_process_gc_mark(am_process_t *proc, am_value_t v) {
    if (!proc || !proc->heap) return -1;

    int32_t ret = 0;

    // 仅处理handle类型的值
    if (!am_value_is_handle(v)) return 0;

    am_handle_t hd = am_value_to_handle(v);
    if (hd == AM_HANDLE_NULL) return 0;

    // handle必须存在于当前进程的堆中
    if (am_heap_has_handle(proc->heap_alloc, proc->heap, hd) != 0) return 0;

    am_value_t obj_value = am_heap_get(proc->heap_alloc, proc->heap, hd);
    if (!am_value_is_ptr(obj_value)) return -1;

    am_object_t *obj = am_value_to_ptr(obj_value);
    if (!obj) return -1;

    // 已经标记过，避免循环引用导致无限递归
    if (am_object_check_alive(obj) == 0) return 0;

    // 根据对象类型进行标记和递归
    int32_t obj_type = obj->type;

    if (obj_type == AM_OBJECT_TYPE_LIST) {
        // 标记当前list对象存活
        am_object_set_alive(obj, 0);

        am_list_t *lst = (am_list_t *)obj;
        for (size_t i = 0; i < lst->length; i++) {
            ret += am_process_gc_mark(proc, lst->children[i]);
        }
    }
    else if (obj_type == AM_OBJECT_TYPE_WSTRING) {
        am_object_set_alive(obj, 0);
    }
    else if (obj_type == AM_OBJECT_TYPE_CLOSURE) {
        am_object_set_alive(obj, 0);

        am_obj_closure_t *closure = (am_obj_closure_t *)obj;
        // 递归标记亲闭包
        ret += am_process_gc_mark(proc, am_make_value_of_handle(closure->parent));

        // 递归标记变量绑定中的handle
        for (size_t i = 0; i < closure->length; i++) {
            am_value_t value = closure->bindings[i].value;
            if (am_value_is_handle(value)) {
                ret += am_process_gc_mark(proc, value);
            }
        }
    }
    else if (obj_type == AM_OBJECT_TYPE_CONTINUATION) {
        // 续体对象本身标记为存活；其stacks中的handle已通过gc_root_helper加入GC根，
        // 因此无需在此递归展开，避免重复遍历。
        am_object_set_alive(obj, 0);
    }

    return ret;
}


// 功能说明：基于存活标记结果，删除所有未被标记存活的非静态对象和对应的handle。成功返回0，失败返回-1
int32_t am_process_gc_sweep(am_process_t *proc) {
    if (!proc || !proc->heap || !proc->heap->table) return -1;

    size_t gcount = 0;
    size_t count = 0;

    size_t heap_count = am_map_length(proc->heap_alloc, proc->heap->table);
    am_value_t *keys = am_map_keys(proc->heap_alloc, proc->heap->table);
    if (!keys && heap_count > 0) return -1;

    for (size_t i = 0; i < heap_count; i++) {
        am_handle_t hd = am_value_to_handle(keys[i]);
        am_value_t value = am_heap_get(proc->heap_alloc, proc->heap, hd);
        if (!am_value_is_ptr(value)) continue;

        am_object_t *obj = am_value_to_ptr(value);
        if (!obj) continue;

        count++;

        // 静态对象永不清理
        if (am_object_check_static(obj) == 0) continue;

        int32_t obj_type = obj->type;
        if (obj_type == AM_OBJECT_TYPE_LIST ||
            obj_type == AM_OBJECT_TYPE_WSTRING ||
            obj_type == AM_OBJECT_TYPE_CLOSURE ||
            obj_type == AM_OBJECT_TYPE_CONTINUATION) {

            if (am_object_check_alive(obj) != 0) {
                // 未被标记为存活：删除handle，同时穿透释放其映射的obj
                am_heap_free_handle(proc->heap_alloc, proc->heap, hd);
                gcount++;
            }
            else {
                // 对于存活对象，将其alive标识清空为否，以便下次gc重新标记
                am_object_set_alive(obj, -1);
            }
        }
    }

    free(keys);

    // printf("[GC] 已清理 %zu / %zu 个对象\n", gcount, count);

    // TODO 暂不实现allocator管理的底层物理内存的整理

    return 0;
}


// 功能说明：对进程执行全量的标记-清除GC。成功返回0，失败返回-1
int32_t am_process_gc(am_process_t *proc) {
    if (!proc || !proc->heap || !proc->heap_alloc || !proc->vm_alloc) return -1;

    // 收集GC根对象
    am_list_t *gcroots = am_list_create(proc->vm_alloc, 4096, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    if (!gcroots) return -1;

    int32_t ret = 0;

    if (am_process_gc_root(proc, gcroots) != 0) {
        ret = -1;
        goto cleanup;
    }

    // 从GC根对象开始递归标记存活对象
    for (size_t i = 0; i < gcroots->length; i++) {
        am_value_t v = am_list_get(proc->vm_alloc, gcroots, i);
        if (am_process_gc_mark(proc, v) != 0) {
            ret = -1;
        }
    }

    // 清除未被标记为存活的非静态对象及其handle
    if (am_process_gc_sweep(proc) != 0) {
        ret = -1;
    }

cleanup:
    am_list_destroy(proc->vm_alloc, gcroots);
    return ret;
}
