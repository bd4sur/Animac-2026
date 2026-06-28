#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <time.h>
#include <math.h>

#include "runtime.h"
#include "native.h"
#include "closure.h"
#include "continuation.h"
#include "list.h"
#include "wstring.h"
#include "heap.h"
#include "map.h"
#include "debug.h"


// ===============================================================================
// 内部辅助函数
// ===============================================================================

// 将数值 TPV 统一转换为浮点数
static am_float_t runtime_number_to_float(am_value_t v) {
    if (am_value_is_float(v)) return am_value_to_float(v);
    if (am_value_is_int(v)) return (am_float_t)am_value_to_int(v);
    if (am_value_is_uint(v)) return (am_float_t)am_value_to_uint(v);
    return 0.0;
}


// 释放 FIFO 中保存的 wstring 对象并销毁列表
static void destroy_fifo(am_runtime_t *rt, am_list_t *fifo) {
    if (!fifo) return;
    for (size_t i = 0; i < fifo->length; i++) {
        am_value_t v = am_list_get(rt->vm_alloc, fifo, i);
        if (am_value_is_ptr(v)) {
            am_object_t *obj = am_value_to_ptr(v);
            if (obj && obj->type == AM_OBJECT_TYPE_WSTRING) {
                am_wstring_destroy(rt->vm_alloc, (am_wstring_t *)obj);
            }
        }
    }
    am_list_destroy(rt->vm_alloc, fifo);
}


// 复制当前闭包的所有绑定到新闭包作为自由变量（用于 load/loadclosure/call）
// new_closure_hd 必须已绑定到一个新创建的 closure 对象。
static int32_t copy_current_closure_bindings_as_free_vars(am_process_t *proc, am_handle_t new_closure_hd) {
    am_obj_closure_t *current = am_process_get_current_closure(proc);
    am_obj_closure_t *new_closure = am_process_get_closure(proc, new_closure_hd);
    if (!new_closure) return -1;

    if (current) {
        for (size_t i = 0; i < current->length; i++) {
            am_binding_t *b = &current->bindings[i];
            new_closure = am_closure_init_free_var(proc->heap_alloc, new_closure, b->varid, b->value);
            if (!new_closure) return -1;
        }
        if (new_closure != am_process_get_closure(proc, new_closure_hd)) {
            am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, new_closure_hd,
                        am_make_value_of_ptr((am_object_t *)new_closure));
        }
    }
    return 0;
}


// 将值以人类可读形式输出到宽字符串缓冲区（简易实现）
static void value_to_wchar_buf(am_process_t *proc, am_value_t v, wchar_t *buf, size_t buf_size) {
    if (buf_size == 0) return;
    buf[0] = L'\0';

    if (am_value_is_handle(v)) {
        am_handle_t hd = am_value_to_handle(v);
        am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, hd);
        if (am_value_is_ptr(obj_val)) {
            am_object_t *obj = am_value_to_ptr(obj_val);
            if (obj->type == AM_OBJECT_TYPE_WSTRING) {
                am_wstring_t *ws = (am_wstring_t *)obj;
                size_t n = ws->length;
                if (n >= buf_size) n = buf_size - 1;
                for (size_t i = 0; i < n; i++) {
                    buf[i] = (wchar_t)am_value_to_wchar(ws->content[i]);
                }
                buf[n] = L'\0';
                return;
            }
            else if (obj->type == AM_OBJECT_TYPE_LIST) {
                swprintf(buf, buf_size, L"#<list>");
                return;
            }
            else if (obj->type == AM_OBJECT_TYPE_CLOSURE) {
                swprintf(buf, buf_size, L"#<closure>");
                return;
            }
            else if (obj->type == AM_OBJECT_TYPE_CONTINUATION) {
                swprintf(buf, buf_size, L"#<continuation>");
                return;
            }
        }
        swprintf(buf, buf_size, L"#<handle>");
    }
    else if (am_value_is_float(v)) {
        swprintf(buf, buf_size, L"%g", (double)am_value_to_float(v));
    }
    else if (am_value_is_int(v)) {
        swprintf(buf, buf_size, L"%lld", (long long)am_value_to_int(v));
    }
    else if (am_value_is_uint(v)) {
        swprintf(buf, buf_size, L"%llu", (unsigned long long)am_value_to_uint(v));
    }
    else if (am_value_is_boolean(v)) {
        swprintf(buf, buf_size, L"%ls", am_value_to_boolean(v) ? L"#t" : L"#f");
    }
    else if (am_value_is_null(v)) {
        swprintf(buf, buf_size, L"#null");
    }
    else if (am_value_is_undefined(v)) {
        swprintf(buf, buf_size, L"#undefined");
    }
    else if (am_value_is_symbol(v)) {
        swprintf(buf, buf_size, L"#<symbol>");
    }
    else if (am_value_is_varid(v)) {
        swprintf(buf, buf_size, L"#<varid>");
    }
    else {
        swprintf(buf, buf_size, L"#<value>");
    }
}


// 递归比较两个值是否结构相等（用于 equal?）
static bool runtime_value_equal(am_process_t *proc, am_value_t a, am_value_t b) {
    if (a == b) return true;

    // 数字按数值比较
    if (am_value_is_number(a) && am_value_is_number(b)) {
        am_float_t fa = runtime_number_to_float(a);
        am_float_t fb = runtime_number_to_float(b);
        return fa == fb;
    }

    // 同为 handle 时按对象类型递归比较
    if (am_value_is_handle(a) && am_value_is_handle(b)) {
        am_handle_t ha = am_value_to_handle(a);
        am_handle_t hb = am_value_to_handle(b);
        am_value_t va = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, ha);
        am_value_t vb = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, hb);
        if (!am_value_is_ptr(va) || !am_value_is_ptr(vb)) return false;

        am_object_t *oa = am_value_to_ptr(va);
        am_object_t *ob = am_value_to_ptr(vb);
        if (oa->type != ob->type) return false;

        if (oa->type == AM_OBJECT_TYPE_LIST) {
            am_list_t *la = (am_list_t *)oa;
            am_list_t *lb = (am_list_t *)ob;
            if (la->length != lb->length) return false;
            for (size_t i = 0; i < la->length; i++) {
                if (!runtime_value_equal(proc, la->children[i], lb->children[i])) return false;
            }
            return true;
        }

        if (oa->type == AM_OBJECT_TYPE_WSTRING) {
            am_wstring_t *wa = (am_wstring_t *)oa;
            am_wstring_t *wb = (am_wstring_t *)ob;
            if (wa->length != wb->length) return false;
            for (size_t i = 0; i < wa->length; i++) {
                if (wa->content[i] != wb->content[i]) return false;
            }
            return true;
        }
    }

    return false;
}


// ===============================================================================
// 第一类：基本存取指令
// ===============================================================================

static int32_t op_store(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    if (!am_value_is_varid(operand)) return -1;

    am_varid_t varid = am_value_to_varid(operand);
    am_value_t value = am_process_pop_operand(proc);

    am_obj_closure_t *current = am_process_get_current_closure(proc);
    if (!current) return -1;

    am_obj_closure_t *new_current = am_closure_init_bound_var(proc->heap_alloc, current, varid, value);
    if (!new_current) return -1;
    if (new_current != current) {
        am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, proc->current_closure_handle,
                    am_make_value_of_ptr((am_object_t *)new_current));
    }

    am_process_step(proc);
    return 0;
}


static int32_t op_load(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    if (!am_value_is_varid(operand)) return -1;

    am_varid_t varid = am_value_to_varid(operand);
    am_value_t value = am_process_dereference(proc, varid);
    if (value == (am_value_t)UINTPTR_MAX) {
        fprintf(stderr, "[Runtime] load: 变量未定义\n");
        return -1;
    }

    // 若值为 iaddr，说明是 lambda 标签解析后的地址，创建闭包
    if (am_value_is_iaddr(value)) {
        am_iaddr_t iaddr = am_value_to_iaddr(value);
        am_handle_t closure_hd = am_process_make_closure(proc, iaddr, proc->current_closure_handle);
        if (closure_hd == AM_HANDLE_NULL) return -1;
        if (copy_current_closure_bindings_as_free_vars(proc, closure_hd) != 0) return -1;
        am_process_push_operand(proc, am_make_value_of_handle(closure_hd));
    }
    else {
        am_process_push_operand(proc, value);
    }

    am_process_step(proc);
    return 0;
}


static int32_t op_loadclosure(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    if (!am_value_is_iaddr(operand)) return -1;

    am_iaddr_t iaddr = am_value_to_iaddr(operand);
    am_handle_t closure_hd = am_process_make_closure(proc, iaddr, proc->current_closure_handle);
    if (closure_hd == AM_HANDLE_NULL) return -1;
    if (copy_current_closure_bindings_as_free_vars(proc, closure_hd) != 0) return -1;

    am_process_push_operand(proc, am_make_value_of_handle(closure_hd));
    am_process_step(proc);
    return 0;
}


static int32_t op_push(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    if (am_process_push_operand(proc, operand) != 0) return -1;
    am_process_step(proc);
    return 0;
}


static int32_t op_pop(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_process_pop_operand(proc);
    am_process_step(proc);
    return 0;
}


static int32_t op_swap(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t top1 = am_process_pop_operand(proc);
    am_value_t top2 = am_process_pop_operand(proc);
    if (top1 == (am_value_t)UINTPTR_MAX || top2 == (am_value_t)UINTPTR_MAX) return -1;
    am_process_push_operand(proc, top1);
    am_process_push_operand(proc, top2);
    am_process_step(proc);
    return 0;
}


static int32_t op_set(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    if (!am_value_is_varid(operand)) return -1;

    am_varid_t varid = am_value_to_varid(operand);
    am_value_t right = am_process_pop_operand(proc);

    am_handle_t current_h = proc->current_closure_handle;
    am_obj_closure_t *current = am_process_get_current_closure(proc);
    if (!current) return -1;

    // 若当前闭包中存在该自由变量，则更新自由变量（带脏标记）
    if (am_closure_has_free_var(proc->heap_alloc, current, varid) == 0) {
        am_obj_closure_t *new_current = am_closure_set_free_var(proc->heap_alloc, current, varid, right);
        if (!new_current) return -1;
        if (new_current != current) {
            am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, current_h,
                        am_make_value_of_ptr((am_object_t *)new_current));
            current = new_current;
        }
    }

    // 沿闭包链上溯，找到约束变量定义位置并更新（带脏标记）
    am_handle_t h = current_h;
    while (h != AM_HANDLE_NULL) {
        am_obj_closure_t *closure = am_process_get_closure(proc, h);
        if (!closure) break;
        if (am_closure_has_bound_var(proc->heap_alloc, closure, varid) == 0) {
            am_obj_closure_t *new_closure = am_closure_set_bound_var(proc->heap_alloc, closure, varid, right);
            if (!new_closure) return -1;
            if (new_closure != closure) {
                am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, h,
                            am_make_value_of_ptr((am_object_t *)new_closure));
            }
            break;
        }
        h = closure->parent;
    }

    am_process_step(proc);
    return 0;
}


// ===============================================================================
// 第二类：分支跳转指令
// ===============================================================================

// 判断变量名是否对应 builtin 操作。
// 基于 AM_GLOBAL_BUILTIN_VAR 与 AM_BUILTIN_OPCODE_MAP：若变量名是 builtin 且映射opcode非 -1，则是 builtin。
static int32_t check_builtin_varid(am_process_t *proc, am_varid_t varid) {
    if (!proc || !proc->var_vocab) return -1;
    wchar_t *name = am_vocab_get(proc->vm_alloc, proc->var_vocab, &varid);
    if (!name) return -1;

    for (size_t i = 0; i < AM_GLOBAL_BUILTIN_VAR_NUM; i++) {
        if (wcscmp(name, AM_GLOBAL_BUILTIN_VAR[i]) == 0) {
            return AM_BUILTIN_OPCODE_MAP[i];
        }
    }
    return -1;
}

static int32_t op_callnative(am_runtime_t *rt, am_process_t *proc, am_value_t operand);

// 功能描述：检查call指令参数（已解析为varid）是否是本地宿主库的调用
// 是返回0，不是返回-1
int32_t am_runtime_check_native_ref(am_runtime_t *rt, am_process_t *proc, am_varid_t varid) {
    (void)rt;
    if (!proc || !proc->var_type) return -1;

    if ((size_t)varid >= proc->var_type->length) return -1;

    am_value_t type_val = am_list_get(proc->vm_alloc, proc->var_type, (size_t)varid);
    if (!am_value_is_uint(type_val)) return -1;

    return (am_value_to_uint(type_val) == (am_uint_t)AM_VAR_TYPE_NATIVE_REF) ? 0 : -1;
}

// call / tailcall 的共享实现。return_target 为 SIZE_MAX 表示尾调用不压栈帧。
static int32_t op_call_async(am_runtime_t *rt, am_process_t *proc, am_value_t operand, am_iaddr_t return_target) {
    (void)rt;

    am_value_t target;
    if (am_value_is_varid(operand)) {
        am_varid_t varid = am_value_to_varid(operand);
        // 判断 native 调用
        if (am_runtime_check_native_ref(rt, proc, varid) == 0) {
            return op_callnative(rt, proc, operand);
        }
        else {
            target = am_process_dereference(proc, varid);
            if (target == (am_value_t)UINTPTR_MAX) {
                fprintf(stderr, "[Runtime] call: 变量未定义\n");
                return -1;
            }
        }
    }
    else {
        target = operand;
    }

    // iaddr：直接调用 lambda
    if (am_value_is_iaddr(target)) {
        am_iaddr_t iaddr = am_value_to_iaddr(target);

        if (return_target != SIZE_MAX) {
            am_value_t closure_val = am_make_value_of_handle(proc->current_closure_handle);
            am_value_t ret_val = am_make_value_of_iaddr(return_target);
            if (am_process_push_stack_frame(proc, closure_val, ret_val) != 0) return -1;
        }
        else {
            // 尾调用优化：若目标与当前闭包地址相同，复用当前闭包
            am_obj_closure_t *cur = am_process_get_current_closure(proc);
            if (cur && cur->iaddr == iaddr) {
                am_process_goto(proc, iaddr);
                return 0;
            }
        }

        am_handle_t closure_hd = am_process_make_closure(proc, iaddr, proc->current_closure_handle);
        if (closure_hd == AM_HANDLE_NULL) return -1;
        if (copy_current_closure_bindings_as_free_vars(proc, closure_hd) != 0) return -1;

        am_process_set_current_closure(proc, closure_hd);
        am_process_goto(proc, iaddr);
        return 0;
    }

    // handle：闭包或 continuation
    if (am_value_is_handle(target)) {
        am_handle_t hd = am_value_to_handle(target);
        am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, hd);
        if (!am_value_is_ptr(obj_val)) return -1;
        am_object_t *obj = am_value_to_ptr(obj_val);

        if (obj->type == AM_OBJECT_TYPE_CLOSURE) {
            am_obj_closure_t *closure = (am_obj_closure_t *)obj;
            if (return_target != SIZE_MAX) {
                am_value_t closure_val = am_make_value_of_handle(proc->current_closure_handle);
                am_value_t ret_val = am_make_value_of_iaddr(return_target);
                if (am_process_push_stack_frame(proc, closure_val, ret_val) != 0) return -1;
            }
            am_process_set_current_closure(proc, hd);
            am_process_goto(proc, closure->iaddr);
            return 0;
        }
        else if (obj->type == AM_OBJECT_TYPE_CONTINUATION) {
            if (return_target != SIZE_MAX) {
                am_value_t closure_val = am_make_value_of_handle(proc->current_closure_handle);
                am_value_t ret_val = am_make_value_of_iaddr(return_target);
                if (am_process_push_stack_frame(proc, closure_val, ret_val) != 0) return -1;
            }
            am_value_t top = am_process_pop_operand(proc);
            am_iaddr_t cont_target = am_process_load_continuation(proc, hd);
            if (cont_target == SIZE_MAX) return -1;
            am_process_push_operand(proc, top);
            am_process_goto(proc, cont_target);
            return 0;
        }
        else {
            fprintf(stderr, "[Runtime] call: 目标对象类型错误\n");
            return -1;
        }
    }

    if (am_value_is_varid(target)) {
        int32_t opcode = check_builtin_varid(proc, am_value_to_varid(target));
        if (opcode >= 0)
            return am_runtime_op_dispatch(rt, proc, (uint32_t)opcode, operand);
    }

    fprintf(stderr, "[Runtime] call: 非法调用目标\n");
    return -1;
}


static int32_t op_call(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    return op_call_async(rt, proc, operand, proc->PC + 1);
}


static int32_t op_tailcall(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    return op_call_async(rt, proc, operand, SIZE_MAX);
}


static int32_t op_callnative(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    if (!proc || !proc->var_vocab) return -1;
    if (!am_value_is_varid(operand)) return -1;

    am_varid_t varid = am_value_to_varid(operand);
    size_t idx = (size_t)varid;
    wchar_t *name = am_vocab_get(proc->vm_alloc, proc->var_vocab, &idx);
    if (!name) return -1;

    // 变量名应为 "LibID.funcName" 形式，且只能有一个点号
    wchar_t *dot = wcschr(name, L'.');
    if (!dot || dot == name || dot[1] == L'\0') {
        fprintf(stderr, "[Runtime] callnative: 非法native变量名\n");
        return -1;
    }
    if (wcschr(dot + 1, L'.')) {
        fprintf(stderr, "[Runtime] callnative: native变量名包含多个点号\n");
        return -1;
    }

    size_t len = wcslen(name);
    wchar_t *buf = (wchar_t *)am_malloc(proc->vm_alloc, (len + 1) * sizeof(wchar_t));
    if (!buf) return -1;
    wcscpy(buf, name);

    wchar_t *prefix = buf;
    wchar_t *suffix = buf + (dot - name);
    *suffix = L'\0';
    suffix++;

    am_native_func_t func = am_native_find_func(prefix, suffix);
    if (!func) {
        fprintf(stderr, "[Runtime] callnative: 未找到native函数 %ls.%ls\n", prefix, suffix);
        am_free(proc->vm_alloc, buf);
        return -1;
    }
    am_free(proc->vm_alloc, buf);

    return func(rt, proc);
}


static int32_t op_return(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t closure_val, ret_val;
    if (am_process_pop_stack_frame(proc, &closure_val, &ret_val) != 0) {
        fprintf(stderr, "[Runtime] return: 函数调用栈为空\n");
        return -1;
    }

    am_process_set_current_closure(proc, am_value_to_handle(closure_val));
    am_process_goto(proc, am_value_to_iaddr(ret_val));
    return 0;
}


static int32_t op_capturecc(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    if (!am_value_is_varid(operand)) return -1;

    am_varid_t varid = am_value_to_varid(operand);
    // 续体返回目标：capturecc 之后固定有 load 和 call 两条指令，返回点位于 PC+3
    am_iaddr_t ret_target = proc->PC + 3;

    am_handle_t cont_hd = am_process_capture_continuation(proc, ret_target);
    if (cont_hd == AM_HANDLE_NULL) return -1;

    am_obj_closure_t *current = am_process_get_current_closure(proc);
    if (!current) return -1;
    am_obj_closure_t *new_current = am_closure_init_bound_var(
        proc->heap_alloc, current, varid, am_make_value_of_handle(cont_hd));
    if (!new_current) return -1;
    if (new_current != current) {
        am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, proc->current_closure_handle,
                    am_make_value_of_ptr((am_object_t *)new_current));
    }

    am_process_step(proc);
    return 0;
}


static int32_t op_iftrue(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    if (!am_value_is_iaddr(operand)) return -1;

    am_value_t condition = am_process_pop_operand(proc);
    if (condition != AM_VALUE_FALSE) {
        am_process_goto(proc, am_value_to_iaddr(operand));
    }
    else {
        am_process_step(proc);
    }
    return 0;
}


static int32_t op_iffalse(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    if (!am_value_is_iaddr(operand)) return -1;

    am_value_t condition = am_process_pop_operand(proc);
    if (condition == AM_VALUE_FALSE) {
        am_process_goto(proc, am_value_to_iaddr(operand));
    }
    else {
        am_process_step(proc);
    }
    return 0;
}


static int32_t op_goto(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    if (!am_value_is_iaddr(operand)) return -1;
    am_process_goto(proc, am_value_to_iaddr(operand));
    return 0;
}


// ===============================================================================
// 第三类：列表操作指令
// ===============================================================================

static int32_t op_car(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t list_val = am_process_pop_operand(proc);
    if (!am_value_is_handle(list_val)) return -1;

    am_handle_t list_hd = am_value_to_handle(list_val);
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, list_hd);
    if (!am_value_is_ptr(obj_val)) return -1;
    am_object_t *obj = am_value_to_ptr(obj_val);
    if (obj->type != AM_OBJECT_TYPE_LIST) return -1;

    am_list_t *lst = (am_list_t *)obj;
    if (lst->length == 0) return -1;

    am_process_push_operand(proc, lst->children[0]);
    am_process_step(proc);
    return 0;
}


static int32_t op_cdr(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t list_val = am_process_pop_operand(proc);
    if (!am_value_is_handle(list_val)) return -1;

    am_handle_t list_hd = am_value_to_handle(list_val);
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, list_hd);
    if (!am_value_is_ptr(obj_val)) return -1;
    am_object_t *obj = am_value_to_ptr(obj_val);
    if (obj->type != AM_OBJECT_TYPE_LIST) return -1;

    am_list_t *lst = (am_list_t *)obj;
    if (lst->length == 0) return -1;

    am_list_t *new_lst = am_list_create(proc->heap_alloc, lst->length, lst->type, list_hd);
    if (!new_lst) return -1;

    for (size_t i = 1; i < lst->length; i++) {
        new_lst = am_list_push(proc->heap_alloc, new_lst, lst->children[i]);
        if (!new_lst) return -1;
    }

    am_handle_t new_hd = am_heap_alloc_handle(proc->vm_alloc, proc->heap_alloc, proc->heap);
    if (new_hd == AM_HANDLE_NULL) {
        am_list_destroy(proc->heap_alloc, new_lst);
        return -1;
    }
    am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, new_hd,
                am_make_value_of_ptr((am_object_t *)new_lst));
    am_process_push_operand(proc, am_make_value_of_handle(new_hd));
    am_process_step(proc);
    return 0;
}


static int32_t op_cons(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t list_val = am_process_pop_operand(proc);
    am_value_t first = am_process_pop_operand(proc);
    if (!am_value_is_handle(list_val)) return -1;

    am_handle_t list_hd = am_value_to_handle(list_val);
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, list_hd);
    if (!am_value_is_ptr(obj_val)) return -1;
    am_object_t *obj = am_value_to_ptr(obj_val);
    if (obj->type != AM_OBJECT_TYPE_LIST) return -1;

    am_list_t *lst = (am_list_t *)obj;
    am_list_t *new_lst = am_list_create(proc->heap_alloc, lst->length + 1, lst->type, list_hd);
    if (!new_lst) return -1;

    new_lst = am_list_push(proc->heap_alloc, new_lst, first);
    if (!new_lst) return -1;
    for (size_t i = 0; i < lst->length; i++) {
        new_lst = am_list_push(proc->heap_alloc, new_lst, lst->children[i]);
        if (!new_lst) return -1;
    }

    am_handle_t new_hd = am_heap_alloc_handle(proc->vm_alloc, proc->heap_alloc, proc->heap);
    if (new_hd == AM_HANDLE_NULL) {
        am_list_destroy(proc->heap_alloc, new_lst);
        return -1;
    }
    am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, new_hd,
                am_make_value_of_ptr((am_object_t *)new_lst));
    am_process_push_operand(proc, am_make_value_of_handle(new_hd));
    am_process_step(proc);
    return 0;
}


static int32_t op_get_item(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t index_val = am_process_pop_operand(proc);
    am_value_t list_val = am_process_pop_operand(proc);
    if (!am_value_is_handle(list_val) || !am_value_is_number(index_val)) return -1;

    am_handle_t list_hd = am_value_to_handle(list_val);
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, list_hd);
    if (!am_value_is_ptr(obj_val)) return -1;
    am_object_t *obj = am_value_to_ptr(obj_val);
    if (obj->type != AM_OBJECT_TYPE_LIST) return -1;

    am_list_t *lst = (am_list_t *)obj;
    am_int_t idx = am_value_is_int(index_val) ? am_value_to_int(index_val) : (am_int_t)am_value_to_uint(index_val);
    if (idx < 0 || (size_t)idx >= lst->length) {
        am_process_push_operand(proc, AM_VALUE_UNDEFINED);
    }
    else {
        am_process_push_operand(proc, lst->children[idx]);
    }
    am_process_step(proc);
    return 0;
}


static int32_t op_set_item(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t value = am_process_pop_operand(proc);
    am_value_t index_val = am_process_pop_operand(proc);
    am_value_t list_val = am_process_pop_operand(proc);
    if (!am_value_is_handle(list_val) || !am_value_is_number(index_val)) return -1;

    am_handle_t list_hd = am_value_to_handle(list_val);
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, list_hd);
    if (!am_value_is_ptr(obj_val)) return -1;
    am_object_t *obj = am_value_to_ptr(obj_val);
    if (obj->type != AM_OBJECT_TYPE_LIST) return -1;

    am_list_t *lst = (am_list_t *)obj;
    am_int_t idx = am_value_is_int(index_val) ? am_value_to_int(index_val) : (am_int_t)am_value_to_uint(index_val);
    if (idx < 0 || (size_t)idx >= lst->length) return -1;

    am_list_set(proc->heap_alloc, lst, (size_t)idx, value);
    am_process_step(proc);
    return 0;
}


static int32_t op_list_push(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t value = am_process_pop_operand(proc);
    am_value_t list_val = am_process_pop_operand(proc);
    if (!am_value_is_handle(list_val)) return -1;

    am_handle_t list_hd = am_value_to_handle(list_val);
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, list_hd);
    if (!am_value_is_ptr(obj_val)) return -1;
    am_object_t *obj = am_value_to_ptr(obj_val);
    if (obj->type != AM_OBJECT_TYPE_LIST) return -1;

    am_list_t *lst = (am_list_t *)obj;
    am_list_t *new_lst = am_list_push(proc->heap_alloc, lst, value);
    if (!new_lst) return -1;
    if (new_lst != lst) {
        am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, list_hd,
                    am_make_value_of_ptr((am_object_t *)new_lst));
    }
    am_process_step(proc);
    return 0;
}


static int32_t op_list_pop(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t list_val = am_process_pop_operand(proc);
    if (!am_value_is_handle(list_val)) return -1;

    am_handle_t list_hd = am_value_to_handle(list_val);
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, list_hd);
    if (!am_value_is_ptr(obj_val)) return -1;
    am_object_t *obj = am_value_to_ptr(obj_val);
    if (obj->type != AM_OBJECT_TYPE_LIST) return -1;

    am_list_t *lst = (am_list_t *)obj;
    am_value_t popped = am_list_pop(proc->heap_alloc, lst);
    am_process_push_operand(proc, popped);
    am_process_step(proc);
    return 0;
}


static int32_t op_length(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t list_val = am_process_pop_operand(proc);
    if (!am_value_is_handle(list_val)) return -1;

    am_handle_t list_hd = am_value_to_handle(list_val);
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, list_hd);
    if (!am_value_is_ptr(obj_val)) return -1;
    am_object_t *obj = am_value_to_ptr(obj_val);
    if (obj->type != AM_OBJECT_TYPE_LIST) return -1;

    am_list_t *lst = (am_list_t *)obj;
    am_process_push_operand(proc, am_make_value_of_uint((am_uint_t)lst->length));
    am_process_step(proc);
    return 0;
}


static int32_t op_concat(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t count_val = am_process_pop_operand(proc);
    if (!am_value_is_number(count_val)) return -1;
    am_int_t count = am_value_is_int(count_val) ? am_value_to_int(count_val) : (am_int_t)am_value_to_uint(count_val);
    if (count < 0) return -1;

    am_value_t *children = (am_value_t *)am_malloc(proc->vm_alloc, (size_t)count * sizeof(am_value_t));
    if (!children && count > 0) return -1;

    for (am_int_t i = count - 1; i >= 0; i--) {
        children[i] = am_process_pop_operand(proc);
    }

    am_list_t *new_lst = am_list_create(proc->heap_alloc, (size_t)count, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    if (!new_lst) {
        am_free(proc->vm_alloc, children);
        return -1;
    }

    for (am_int_t i = 0; i < count; i++) {
        new_lst = am_list_push(proc->heap_alloc, new_lst, children[i]);
        if (!new_lst) {
            am_free(proc->vm_alloc, children);
            return -1;
        }
    }

    am_handle_t new_hd = am_heap_alloc_handle(proc->vm_alloc, proc->heap_alloc, proc->heap);
    if (new_hd == AM_HANDLE_NULL) {
        am_free(proc->vm_alloc, children);
        am_list_destroy(proc->heap_alloc, new_lst);
        return -1;
    }
    am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, new_hd,
                am_make_value_of_ptr((am_object_t *)new_lst));

    // 设置子列表的 parent 字段
    for (size_t i = 0; i < new_lst->length; i++) {
        am_value_t child = new_lst->children[i];
        if (am_value_is_handle(child)) {
            am_handle_t child_hd = am_value_to_handle(child);
            am_value_t child_obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, child_hd);
            if (am_value_is_ptr(child_obj_val)) {
                am_object_t *child_obj = am_value_to_ptr(child_obj_val);
                if (child_obj->type == AM_OBJECT_TYPE_LIST) {
                    ((am_list_t *)child_obj)->parent = new_hd;
                }
            }
        }
    }

    am_free(proc->vm_alloc, children);
    am_process_push_operand(proc, am_make_value_of_handle(new_hd));
    am_process_step(proc);
    return 0;
}


static int32_t op_duplicate(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;

    am_value_t val = am_process_pop_operand(proc);
    if (!am_value_is_handle(val)) return -1;

    am_handle_t hd = am_value_to_handle(val);
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, hd);
    if (!am_value_is_ptr(obj_val)) return -1;
    am_object_t *obj = am_value_to_ptr(obj_val);

    if (obj->type == AM_OBJECT_TYPE_LIST) {
        am_list_t *lst = (am_list_t *)obj;
        am_list_t *copy = am_list_copy(proc->heap_alloc, lst);
        if (!copy) return -1;
        am_handle_t new_hd = am_heap_alloc_handle(proc->vm_alloc, proc->heap_alloc, proc->heap);
        if (new_hd == AM_HANDLE_NULL) {
            am_list_destroy(proc->heap_alloc, copy);
            return -1;
        }
        am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, new_hd,
                    am_make_value_of_ptr((am_object_t *)copy));
        am_process_push_operand(proc, am_make_value_of_handle(new_hd));
    }
    else if (obj->type == AM_OBJECT_TYPE_WSTRING) {
        am_wstring_t *ws = (am_wstring_t *)obj;
        am_wstring_t *copy = am_wstring_copy(proc->heap_alloc, ws);
        if (!copy) return -1;
        am_handle_t new_hd = am_heap_alloc_handle(proc->vm_alloc, proc->heap_alloc, proc->heap);
        if (new_hd == AM_HANDLE_NULL) {
            am_wstring_destroy(proc->heap_alloc, copy);
            return -1;
        }
        am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, new_hd,
                    am_make_value_of_ptr((am_object_t *)copy));
        am_process_push_operand(proc, am_make_value_of_handle(new_hd));
    }
    else {
        // 其他类型暂不做深拷贝，直接返回原 handle
        am_process_push_operand(proc, val);
    }

    am_process_step(proc);
    return 0;
}


// ===============================================================================
// 第四类：算术逻辑运算和谓词
// ===============================================================================

static int32_t op_add(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    if (!am_value_is_number(a) || !am_value_is_number(b)) return -1;
    am_float_t result = runtime_number_to_float(b) + runtime_number_to_float(a);
    am_process_push_operand(proc, am_make_value_of_float(result));
    am_process_step(proc);
    return 0;
}


static int32_t op_sub(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    if (!am_value_is_number(a) || !am_value_is_number(b)) return -1;
    am_float_t result = runtime_number_to_float(b) - runtime_number_to_float(a);
    am_process_push_operand(proc, am_make_value_of_float(result));
    am_process_step(proc);
    return 0;
}


static int32_t op_mul(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    if (!am_value_is_number(a) || !am_value_is_number(b)) return -1;
    am_float_t result = runtime_number_to_float(b) * runtime_number_to_float(a);
    am_process_push_operand(proc, am_make_value_of_float(result));
    am_process_step(proc);
    return 0;
}


static int32_t op_div(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    if (!am_value_is_number(a) || !am_value_is_number(b)) return -1;
    am_float_t fa = runtime_number_to_float(a);
    if (fa == 0.0) {
        fprintf(stderr, "[Runtime] 除零错误\n");
        return -1;
    }
    am_float_t result = runtime_number_to_float(b) / fa;
    am_process_push_operand(proc, am_make_value_of_float(result));
    am_process_step(proc);
    return 0;
}


static int32_t op_mod(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    if (!am_value_is_number(a) || !am_value_is_number(b)) return -1;
    am_float_t result = fmod(runtime_number_to_float(b), runtime_number_to_float(a));
    am_process_push_operand(proc, am_make_value_of_float(result));
    am_process_step(proc);
    return 0;
}


static int32_t op_pow(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    if (!am_value_is_number(a) || !am_value_is_number(b)) return -1;
    am_float_t result = pow(runtime_number_to_float(b), runtime_number_to_float(a));
    am_process_push_operand(proc, am_make_value_of_float(result));
    am_process_step(proc);
    return 0;
}


static int32_t op_eq(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    am_value_t result = (b == a) ? AM_VALUE_TRUE : AM_VALUE_FALSE;
    am_process_push_operand(proc, result);
    am_process_step(proc);
    return 0;
}


static int32_t op_eqv(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    bool equal;
    if (am_value_is_number(a) && am_value_is_number(b)) {
        equal = (runtime_number_to_float(a) == runtime_number_to_float(b));
    }
    else {
        equal = (a == b);
    }
    am_process_push_operand(proc, equal ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_equal(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    bool equal = runtime_value_equal(proc, b, a);
    am_process_push_operand(proc, equal ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_ge(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    if (!am_value_is_number(a) || !am_value_is_number(b)) return -1;
    bool result = runtime_number_to_float(b) >= runtime_number_to_float(a);
    am_process_push_operand(proc, result ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_le(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    if (!am_value_is_number(a) || !am_value_is_number(b)) return -1;
    bool result = runtime_number_to_float(b) <= runtime_number_to_float(a);
    am_process_push_operand(proc, result ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_gt(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    if (!am_value_is_number(a) || !am_value_is_number(b)) return -1;
    bool result = runtime_number_to_float(b) > runtime_number_to_float(a);
    am_process_push_operand(proc, result ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_lt(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    if (!am_value_is_number(a) || !am_value_is_number(b)) return -1;
    bool result = runtime_number_to_float(b) < runtime_number_to_float(a);
    am_process_push_operand(proc, result ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_not(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_process_push_operand(proc, (a == AM_VALUE_FALSE) ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_and(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    bool result = (a != AM_VALUE_FALSE) && (b != AM_VALUE_FALSE);
    am_process_push_operand(proc, result ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_or(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t a = am_process_pop_operand(proc);
    am_value_t b = am_process_pop_operand(proc);
    bool result = (a != AM_VALUE_FALSE) || (b != AM_VALUE_FALSE);
    am_process_push_operand(proc, result ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_isnull(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t v = am_process_pop_operand(proc);
    bool result = false;
    if (am_value_is_null(v)) {
        result = true;
    }
    else if (am_value_is_handle(v)) {
        am_handle_t hd = am_value_to_handle(v);
        am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, hd);
        if (am_value_is_ptr(obj_val)) {
            am_object_t *obj = am_value_to_ptr(obj_val);
            if (obj->type == AM_OBJECT_TYPE_LIST && ((am_list_t *)obj)->length == 0) {
                result = true;
            }
        }
    }
    am_process_push_operand(proc, result ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_isundef(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t v = am_process_pop_operand(proc);
    am_process_push_operand(proc, am_value_is_undefined(v) ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_isatom(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t v = am_process_pop_operand(proc);
    am_process_push_operand(proc, !am_value_is_handle(v) ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_islist(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t v = am_process_pop_operand(proc);
    bool result = false;
    if (am_value_is_handle(v)) {
        am_handle_t hd = am_value_to_handle(v);
        am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, hd);
        if (am_value_is_ptr(obj_val)) {
            am_object_t *obj = am_value_to_ptr(obj_val);
            if (obj->type == AM_OBJECT_TYPE_LIST) {
                result = true;
            }
        }
    }
    am_process_push_operand(proc, result ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_isnumber(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t v = am_process_pop_operand(proc);
    am_process_push_operand(proc, am_value_is_number(v) ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_isnan(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t v = am_process_pop_operand(proc);
    bool result = false;
    if (am_value_is_float(v)) {
        result = isnan(runtime_number_to_float(v));
    }
    am_process_push_operand(proc, result ? AM_VALUE_TRUE : AM_VALUE_FALSE);
    am_process_step(proc);
    return 0;
}


static int32_t op_typeof(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_value_t v = am_process_pop_operand(proc);
    const wchar_t *type_name = L"unknown";

    if (am_value_is_ptr(v)) {
        am_object_t *obj = am_value_to_ptr(v);
        switch (obj->type) {
            case AM_OBJECT_TYPE_LIST:         type_name = L"list"; break;
            case AM_OBJECT_TYPE_MAP:          type_name = L"map"; break;
            case AM_OBJECT_TYPE_WSTRING:      type_name = L"string"; break;
            case AM_OBJECT_TYPE_PORT:         type_name = L"port"; break;
            case AM_OBJECT_TYPE_CLOSURE:      type_name = L"closure"; break;
            case AM_OBJECT_TYPE_CONTINUATION: type_name = L"continuation"; break;
            case AM_OBJECT_TYPE_FRAME:        type_name = L"frame"; break;
            case AM_OBJECT_TYPE_ILCODE:       type_name = L"ilcode"; break;
            case AM_OBJECT_TYPE_BOX:          type_name = L"box"; break;
            case AM_OBJECT_TYPE_TOKEN:        type_name = L"token"; break;
            case AM_OBJECT_TYPE_SCOPE:        type_name = L"scope"; break;
            case AM_OBJECT_TYPE_VOCAB:        type_name = L"vocab"; break;
            default:                          type_name = L"object"; break;
        }
    }
    else if (am_value_is_handle(v))       type_name = L"handle";
    else if (am_value_is_varid(v))        type_name = L"varid";
    else if (am_value_is_symbol(v))       type_name = L"symbol";
    else if (am_value_is_iaddr(v))        type_name = L"iaddr";
    else if (am_value_is_label(v))        type_name = L"label";
    else if (am_value_is_boolean(v))      type_name = L"boolean";
    else if (am_value_is_null(v))         type_name = L"null";
    else if (am_value_is_undefined(v))    type_name = L"undefined";
    else if (am_value_is_uint(v) ||
             am_value_is_int(v) ||
             am_value_is_float(v))        type_name = L"number";
    else if (am_value_is_wchar(v))        type_name = L"wchar";

    am_wstring_t *ws = am_wstring_create(proc->heap_alloc, (wchar_t *)type_name, wcslen(type_name));
    if (!ws) return -1;
    am_handle_t hd = am_heap_alloc_handle(proc->vm_alloc, proc->heap_alloc, proc->heap);
    if (hd == AM_HANDLE_NULL) {
        am_wstring_destroy(proc->heap_alloc, ws);
        return -1;
    }
    if (am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, hd,
                    am_make_value_of_ptr((am_object_t *)ws)) != 0) {
        am_wstring_destroy(proc->heap_alloc, ws);
        am_heap_free_handle(proc->vm_alloc, proc->heap_alloc, proc->heap, hd);
        return -1;
    }
    am_process_push_operand(proc, am_make_value_of_handle(hd));
    am_process_step(proc);
    return 0;
}


// ===============================================================================
// 第五类：其他指令
// ===============================================================================

static int32_t op_fork(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)proc;
    (void)operand;
    // TODO: 实现 fork
    fprintf(stderr, "[Runtime] fork 指令尚未实现\n");
    return -1;
}


static int32_t op_display(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)operand;
    am_value_t content = am_process_pop_operand(proc);

    // 列表对象使用专门的字符串化函数
    if (am_value_is_handle(content)) {
        am_handle_t hd = am_value_to_handle(content);
        am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, hd);
        if (am_value_is_ptr(obj_val)) {
            am_object_t *obj = am_value_to_ptr(obj_val);
            if (obj->type == AM_OBJECT_TYPE_LIST) {
                size_t len = 0;
                wchar_t *str = am_process_list_to_string(proc, hd, &len);
                if (str) {
                    am_runtime_output(rt, str);
                    am_free(proc->vm_alloc, str);
                    am_process_step(proc);
                    return 0;
                }
            }
        }
    }

    wchar_t buf[1024];
    value_to_wchar_buf(proc, content, buf, 1024);
    am_runtime_output(rt, buf);
    am_process_step(proc);
    return 0;
}


static int32_t op_newline(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)operand;
    am_runtime_output(rt, L"\n");
    am_process_step(proc);
    return 0;
}


static int32_t op_read(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    // TODO: 端口读取尚未实现
    am_process_push_operand(proc, AM_VALUE_UNDEFINED);
    am_process_step(proc);
    return 0;
}


static int32_t op_write(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    // TODO: 端口写入尚未实现
    am_process_pop_operand(proc);
    am_process_pop_operand(proc);
    am_process_step(proc);
    return 0;
}


static int32_t op_nop(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_process_step(proc);
    return 0;
}


static int32_t op_pause(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_process_set_state(proc, AM_PROCESS_STATE_SUSPENDED);
    return 0;
}


static int32_t op_halt(am_runtime_t *rt, am_process_t *proc, am_value_t operand) {
    (void)rt;
    (void)operand;
    am_process_set_state(proc, AM_PROCESS_STATE_STOPPED);
    return 0;
}


// ===============================================================================
// 生命周期
// ===============================================================================

am_runtime_t *am_runtime_create(am_allocator_t *vm_alloc, am_allocator_t *heap_alloc, const wchar_t *base_dir) {
    if (!vm_alloc || !heap_alloc) return NULL;

    am_runtime_t *rt = (am_runtime_t *)am_calloc(vm_alloc, sizeof(am_runtime_t));
    if (!rt) return NULL;

    rt->vm_alloc = vm_alloc;
    rt->heap_alloc = heap_alloc;

    if (base_dir) {
        size_t len = wcslen(base_dir);
        rt->working_dir = (wchar_t *)am_malloc(vm_alloc, (len + 1) * sizeof(wchar_t));
        if (rt->working_dir) {
            memcpy(rt->working_dir, base_dir, (len + 1) * sizeof(wchar_t));
        }
    }

    rt->process_pool_capacity = 16;
    rt->process_pool = (am_process_t **)am_calloc(vm_alloc, rt->process_pool_capacity * sizeof(am_process_t *));
    if (!rt->process_pool) {
        am_free(vm_alloc, rt);
        return NULL;
    }
    rt->process_poll_counter = 0;

    rt->process_queue = am_list_create(vm_alloc, 16, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    rt->input_fifo = am_list_create(vm_alloc, 16, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    rt->output_fifo = am_list_create(vm_alloc, 16, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    rt->error_fifo = am_list_create(vm_alloc, 16, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);

    if (!rt->process_queue || !rt->input_fifo || !rt->output_fifo || !rt->error_fifo) {
        am_runtime_destroy(rt);
        return NULL;
    }

    rt->callback_on_tick = NULL;
    rt->callback_on_event = NULL;
    rt->callback_on_halt = NULL;
    rt->callback_on_error = NULL;

    rt->tick_counter = 0;
    rt->gc_timestamp = time(NULL);

    return rt;
}


int32_t am_runtime_destroy(am_runtime_t *rt) {
    if (!rt) return 0;

    if (rt->process_pool) {
        for (size_t i = 0; i < rt->process_poll_counter; i++) {
            if (rt->process_pool[i]) {
                am_process_destroy(rt->process_pool[i]);
            }
        }
        am_free(rt->vm_alloc, rt->process_pool);
        rt->process_pool = NULL;
    }

    if (rt->process_queue) {
        am_list_destroy(rt->vm_alloc, rt->process_queue);
        rt->process_queue = NULL;
    }

    destroy_fifo(rt, rt->input_fifo);
    rt->input_fifo = NULL;
    destroy_fifo(rt, rt->output_fifo);
    rt->output_fifo = NULL;
    destroy_fifo(rt, rt->error_fifo);
    rt->error_fifo = NULL;

    if (rt->working_dir) {
        am_free(rt->vm_alloc, rt->working_dir);
        rt->working_dir = NULL;
    }

    am_free(rt->vm_alloc, rt);
    return 0;
}


// ===============================================================================
// 模块与进程管理
// ===============================================================================

am_pid_t am_runtime_load_module(am_runtime_t *rt, am_module_t *mod) {
    if (!rt || !mod) return (am_pid_t)-1;

    am_process_t *proc = am_process_load_from_module(rt->vm_alloc, rt->heap_alloc, mod);
    if (!proc) return (am_pid_t)-1;

    am_pid_t pid = rt->process_poll_counter;
    proc->pid = pid;
    proc->parent_pid = 0;

    if (pid >= rt->process_pool_capacity) {
        size_t new_cap = rt->process_pool_capacity * 2;
        am_process_t **new_pool = (am_process_t **)am_realloc(
            rt->vm_alloc, rt->process_pool, new_cap * sizeof(am_process_t *));
        if (!new_pool) {
            am_process_destroy(proc);
            return (am_pid_t)-1;
        }
        rt->process_pool = new_pool;
        rt->process_pool_capacity = new_cap;
    }

    rt->process_pool[pid] = proc;
    rt->process_poll_counter++;

    am_value_t pid_val = am_make_value_of_uint((am_uint_t)pid);
    am_list_t *new_queue = am_list_push(rt->vm_alloc, rt->process_queue, pid_val);
    if (!new_queue) {
        rt->process_pool[pid] = NULL;
        rt->process_poll_counter--;
        am_process_destroy(proc);
        return (am_pid_t)-1;
    }
    rt->process_queue = new_queue;

    return pid;
}


am_process_t *am_runtime_get_process(am_runtime_t *rt, am_pid_t pid) {
    if (!rt || pid >= rt->process_poll_counter) return NULL;
    return rt->process_pool[pid];
}


am_pid_t am_load_module(am_runtime_t *rt, am_module_t *mod) {
    return am_runtime_load_module(rt, mod);
}


// ===============================================================================
// 调度器
// ===============================================================================


int32_t am_runtime_op_dispatch(am_runtime_t *rt, am_process_t *proc, uint32_t opcode, am_value_t operand) {
    switch (opcode) {
        case AM_VM_OP_nop:         return op_nop(rt, proc, operand);
        case AM_VM_OP_store:       return op_store(rt, proc, operand);
        case AM_VM_OP_load:        return op_load(rt, proc, operand);
        case AM_VM_OP_loadclosure: return op_loadclosure(rt, proc, operand);
        case AM_VM_OP_push:        return op_push(rt, proc, operand);
        case AM_VM_OP_pop:         return op_pop(rt, proc, operand);
        case AM_VM_OP_swap:        return op_swap(rt, proc, operand);
        case AM_VM_OP_set:         return op_set(rt, proc, operand);
        case AM_VM_OP_call:        return op_call(rt, proc, operand);
        case AM_VM_OP_callnative:  return op_callnative(rt, proc, operand);
        case AM_VM_OP_tailcall:    return op_tailcall(rt, proc, operand);
        case AM_VM_OP_return:      return op_return(rt, proc, operand);
        case AM_VM_OP_capturecc:   return op_capturecc(rt, proc, operand);
        case AM_VM_OP_iftrue:      return op_iftrue(rt, proc, operand);
        case AM_VM_OP_iffalse:     return op_iffalse(rt, proc, operand);
        case AM_VM_OP_goto:        return op_goto(rt, proc, operand);
        case AM_VM_OP_read:        return op_read(rt, proc, operand);
        case AM_VM_OP_write:       return op_write(rt, proc, operand);
        case AM_VM_OP_pause:       return op_pause(rt, proc, operand);
        case AM_VM_OP_halt:        return op_halt(rt, proc, operand);
        case AM_VM_OP_fork:        return op_fork(rt, proc, operand);
        case AM_VM_OP_display:     return op_display(rt, proc, operand);
        case AM_VM_OP_newline:     return op_newline(rt, proc, operand);
        case AM_VM_OP_add:         return op_add(rt, proc, operand);
        case AM_VM_OP_sub:         return op_sub(rt, proc, operand);
        case AM_VM_OP_mul:         return op_mul(rt, proc, operand);
        case AM_VM_OP_div:         return op_div(rt, proc, operand);
        case AM_VM_OP_mod:         return op_mod(rt, proc, operand);
        case AM_VM_OP_pow:         return op_pow(rt, proc, operand);
        case AM_VM_OP_eq:          return op_eq(rt, proc, operand);
        case AM_VM_OP_eqv:         return op_eqv(rt, proc, operand);
        case AM_VM_OP_equal:       return op_equal(rt, proc, operand);
        case AM_VM_OP_ge:          return op_ge(rt, proc, operand);
        case AM_VM_OP_le:          return op_le(rt, proc, operand);
        case AM_VM_OP_gt:          return op_gt(rt, proc, operand);
        case AM_VM_OP_lt:          return op_lt(rt, proc, operand);
        case AM_VM_OP_not:         return op_not(rt, proc, operand);
        case AM_VM_OP_and:         return op_and(rt, proc, operand);
        case AM_VM_OP_or:          return op_or(rt, proc, operand);
        case AM_VM_OP_isnull:      return op_isnull(rt, proc, operand);
        case AM_VM_OP_isundef:     return op_isundef(rt, proc, operand);
        case AM_VM_OP_isatom:      return op_isatom(rt, proc, operand);
        case AM_VM_OP_islist:      return op_islist(rt, proc, operand);
        case AM_VM_OP_isnumber:    return op_isnumber(rt, proc, operand);
        case AM_VM_OP_isnan:       return op_isnan(rt, proc, operand);
        case AM_VM_OP_typeof:      return op_typeof(rt, proc, operand);
        case AM_VM_OP_car:         return op_car(rt, proc, operand);
        case AM_VM_OP_cdr:         return op_cdr(rt, proc, operand);
        case AM_VM_OP_cons:        return op_cons(rt, proc, operand);
        case AM_VM_OP_get_item:    return op_get_item(rt, proc, operand);
        case AM_VM_OP_set_item:    return op_set_item(rt, proc, operand);
        case AM_VM_OP_list_push:   return op_list_push(rt, proc, operand);
        case AM_VM_OP_list_pop:    return op_list_pop(rt, proc, operand);
        case AM_VM_OP_length:      return op_length(rt, proc, operand);
        case AM_VM_OP_concat:      return op_concat(rt, proc, operand);
        case AM_VM_OP_duplicate:   return op_duplicate(rt, proc, operand);
        default:
            fprintf(stderr, "[Runtime] 未知指令: %u\n", opcode);
            return -1;
    }
}


int32_t am_runtime_execute(am_runtime_t *rt, am_process_t *proc) {
    if (!rt || !proc) return -1;

    uint32_t opcode;
    am_value_t operand;
    if (am_process_current_instruction(proc, &opcode, &operand) != 0) {
        return -1;
    }

    // printf("Exec: PC=%zu | OpCode=%u | Oprand=%zu(varid=%zu)\n", proc->PC, opcode, operand, am_value_to_varid(operand));
    return am_runtime_op_dispatch(rt, proc, opcode, operand);
}


int32_t am_runtime_tick(am_runtime_t *rt, uint32_t timeslice) {
    if (!rt || !rt->process_queue) return AM_VM_STATE_IDLE;
    if (rt->process_queue->length == 0) return AM_VM_STATE_IDLE;

    am_value_t pid_val = am_list_shift(rt->vm_alloc, rt->process_queue);
    if (!am_value_is_uint(pid_val)) return AM_VM_STATE_IDLE;

    am_pid_t pid = (am_pid_t)am_value_to_uint(pid_val);
    if (pid >= rt->process_poll_counter || !rt->process_pool[pid]) {
        return AM_VM_STATE_IDLE;
    }

    am_process_t *proc = rt->process_pool[pid];
    proc->state = AM_PROCESS_STATE_RUNNING;

    while (timeslice > 0 && proc->state == AM_PROCESS_STATE_RUNNING) {
        if (am_runtime_execute(rt, proc) != 0) {
            proc->state = AM_PROCESS_STATE_STOPPED;
            if (rt->callback_on_error) rt->callback_on_error(rt);
            fprintf(stderr, "[Runtime] 指令执行异常\n");
            break;
        }
        timeslice--;
    }

    if (proc->state == AM_PROCESS_STATE_RUNNING) {
        proc->state = AM_PROCESS_STATE_READY;
        am_list_t *new_queue = am_list_push(rt->vm_alloc, rt->process_queue, pid_val);
        if (!new_queue) {
            // 入队失败，停止进程以避免丢失
            proc->state = AM_PROCESS_STATE_STOPPED;
            return AM_VM_STATE_IDLE;
        }
        rt->process_queue = new_queue;
    }

    rt->tick_counter++;
    if (rt->callback_on_tick) rt->callback_on_tick(rt);

    return (rt->process_queue->length > 0) ? AM_VM_STATE_RUNNING : AM_VM_STATE_IDLE;
}


int32_t am_runtime_event_handler(am_runtime_t *rt) {
    if (!rt) return AM_VM_STATE_IDLE;

    int32_t vm_state = AM_VM_STATE_IDLE;
    for (int i = 0; i < AM_COMPUTATION_PHASE_LENGTH; i++) {
        vm_state = am_runtime_tick(rt, 10000);
        if (vm_state == AM_VM_STATE_IDLE) break;
    }

#if AM_ENABLE_GC
    // time_t now = time(NULL);
    // if (now - rt->gc_timestamp >= AM_GC_INTERVAL) {
    //     rt->gc_timestamp = now;
        for (size_t i = 0; i < rt->process_queue->length; i++) {
            am_value_t pid_val = am_list_get(rt->vm_alloc, rt->process_queue, i);
            if (!am_value_is_uint(pid_val)) continue;
            am_pid_t pid = (am_pid_t)am_value_to_uint(pid_val);
            if (pid < rt->process_poll_counter && rt->process_pool[pid]) {
                am_process_gc(rt->process_pool[pid]);
            }
        }
    // }
#endif

    if (rt->callback_on_event) rt->callback_on_event(rt);
    return vm_state;
}


void am_runtime_start(am_runtime_t *rt) {
    if (!rt) return;

    while (1) {
        int32_t vm_state = am_runtime_event_handler(rt);
        if (vm_state == AM_VM_STATE_IDLE) {
            if (rt->callback_on_halt) rt->callback_on_halt(rt);
            break;
        }
    }
}


void am_start(am_runtime_t *rt) {
    am_runtime_start(rt);
}


// ===============================================================================
// 控制台输入输出
// ===============================================================================

void am_runtime_output(am_runtime_t *rt, const wchar_t *str) {
    if (!rt || !str) return;
    printf("%ls", str);
    fflush(stdout);
    am_wstring_t *ws = am_wstring_create(rt->vm_alloc, (wchar_t *)str, wcslen(str));
    if (ws && rt->output_fifo) {
        am_list_t *new_fifo = am_list_push(rt->vm_alloc, rt->output_fifo,
                                           am_make_value_of_ptr((am_object_t *)ws));
        if (new_fifo) rt->output_fifo = new_fifo;
        else am_wstring_destroy(rt->vm_alloc, ws);
    }
}


void am_runtime_error(am_runtime_t *rt, const wchar_t *str) {
    if (!rt || !str) return;
    fprintf(stderr, "%ls", str);
    am_wstring_t *ws = am_wstring_create(rt->vm_alloc, (wchar_t *)str, wcslen(str));
    if (ws && rt->error_fifo) {
        am_list_t *new_fifo = am_list_push(rt->vm_alloc, rt->error_fifo,
                                           am_make_value_of_ptr((am_object_t *)ws));
        if (new_fifo) rt->error_fifo = new_fifo;
        else am_wstring_destroy(rt->vm_alloc, ws);
    }
}
