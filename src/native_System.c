#include "native_System.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>

#include "wstring.h"


// ===============================================================================
// 内部辅助函数
// ===============================================================================

// 将数值 TPV 统一转换为浮点数
static am_float_t system_number_to_float(am_value_t v) {
    if (am_value_is_float(v)) return am_value_to_float(v);
    if (am_value_is_int(v)) return (am_float_t)am_value_to_int(v);
    if (am_value_is_uint(v)) return (am_float_t)am_value_to_uint(v);
    return 0.0;
}


// 从操作数栈中弹出一个数值，统一转换为 float。
// 成功返回 true，失败返回 false。
static bool native_pop_number(am_process_t *proc, am_float_t *out) {
    am_value_t v = am_process_pop_operand(proc);
    if (v == (am_value_t)UINTPTR_MAX) return false;

    if (am_value_is_float(v)) {
        *out = am_value_to_float(v);
        return true;
    }
    if (am_value_is_int(v)) {
        *out = (am_float_t)am_value_to_int(v);
        return true;
    }
    if (am_value_is_uint(v)) {
        *out = (am_float_t)am_value_to_uint(v);
        return true;
    }
    return false;
}


// 从操作数栈中弹出一个闭包把柄。
// 成功返回 true，失败返回 false。
static bool native_pop_closure(am_process_t *proc, am_handle_t *out) {
    am_value_t v = am_process_pop_operand(proc);
    if (v == (am_value_t)UINTPTR_MAX) return false;
    if (!am_value_is_handle(v)) return false;

    am_handle_t hd = am_value_to_handle(v);
    am_obj_closure_t *closure = am_process_get_closure(proc, hd);
    if (!closure) return false;

    *out = hd;
    return true;
}


// 将 wstring 对象的内容复制到堆分配器新分配的 wchar_t 缓冲区中。
// 调用者负责使用 proc->heap_alloc 释放返回的缓冲区。
static wchar_t *wstring_content_to_buffer(am_process_t *proc, am_wstring_t *ws) {
    if (!ws || ws->length == 0) return NULL;

    wchar_t *buf = (wchar_t *)am_malloc(proc->heap_alloc, (ws->length + 1) * sizeof(wchar_t));
    if (!buf) return NULL;

    for (size_t i = 0; i < ws->length; i++) {
        buf[i] = (wchar_t)am_value_to_wchar(ws->content[i]);
    }
    buf[ws->length] = L'\0';
    return buf;
}


// 从操作数栈中弹出一个字符串对象，并将其内容转换为多字节 char* 缓冲区。
// 成功返回 true，*out 为堆分配器分配的缓冲区（调用者负责释放），失败返回 false。
static bool native_pop_wstring_as_mb(am_process_t *proc, char **out) {
    am_value_t v = am_process_pop_operand(proc);
    if (v == (am_value_t)UINTPTR_MAX) return false;
    if (!am_value_is_handle(v)) return false;

    am_handle_t hd = am_value_to_handle(v);
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, hd);
    if (!am_value_is_ptr(obj_val)) return false;

    am_object_t *obj = am_value_to_ptr(obj_val);
    if (obj->type != AM_OBJECT_TYPE_WSTRING) return false;

    am_wstring_t *ws = (am_wstring_t *)obj;
    wchar_t *wbuf = wstring_content_to_buffer(proc, ws);
    if (ws->length > 0 && !wbuf) return false;

    size_t mb_len = wcstombs(NULL, wbuf ? wbuf : L"", 0);
    if (mb_len == (size_t)-1) {
        if (wbuf) am_free(proc->heap_alloc, wbuf);
        return false;
    }

    char *mb_buf = (char *)am_malloc(proc->heap_alloc, mb_len + 1);
    if (!mb_buf) {
        if (wbuf) am_free(proc->heap_alloc, wbuf);
        return false;
    }

    wcstombs(mb_buf, wbuf ? wbuf : L"", mb_len + 1);
    mb_buf[mb_len] = '\0';

    if (wbuf) am_free(proc->heap_alloc, wbuf);
    *out = mb_buf;
    return true;
}


// 从 wchar_t 缓冲区创建字符串对象并压回操作数栈。
// 成功返回 0，失败返回 -1。
static int32_t native_push_wstring_buf(am_process_t *proc, const wchar_t *buf, size_t len) {
    am_wstring_t *ws = am_wstring_create(proc->heap_alloc, (wchar_t *)buf, len);
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

    if (am_process_push_operand(proc, am_make_value_of_handle(hd)) != 0) return -1;
    am_process_step(proc);
    return 0;
}


// 将 float 结果压回操作数栈；若结果为 NaN，则压入 null。
static int32_t native_push_float_or_null(am_process_t *proc, am_float_t result) {
    if (isnan(result)) {
        if (am_process_push_operand(proc, AM_VALUE_NULL) != 0) return -1;
    }
    else {
        if (am_process_push_operand(proc, am_make_value_of_float(result)) != 0) return -1;
    }
    am_process_step(proc);
    return 0;
}


// 设置闭包对象的 keepalive 标记，防止异步回调闭包被 GC 回收。
static void native_keepalive_closure(am_process_t *proc, am_handle_t hd) {
    am_value_t obj_val = am_heap_get(proc->vm_alloc, proc->heap_alloc, proc->heap, hd);
    if (!am_value_is_ptr(obj_val)) return;
    am_object_t *obj = am_value_to_ptr(obj_val);
    if (obj->type == AM_OBJECT_TYPE_CLOSURE) {
        am_object_set_keepalive(obj, 0);
    }
}


// ===============================================================================
// System.fork 内部辅助函数
// ===============================================================================

// 堆对象深拷贝的映射表条目
typedef struct {
    am_handle_t old_handle;
    am_handle_t new_handle;
    am_object_t *old_obj;
    am_object_t *new_obj;
} am_fork_heap_mapping_t;


typedef struct {
    am_allocator_t *vm_alloc;
    am_allocator_t *heap_alloc;
    am_heap_t *src_heap;
    am_heap_t *dst_heap;
    am_fork_heap_mapping_t *entries;
    size_t count;
    size_t capacity;
} am_fork_heap_ctx_t;


static int32_t am_fork_heap_ctx_init(am_fork_heap_ctx_t *ctx,
                                     am_allocator_t *vm_alloc,
                                     am_allocator_t *heap_alloc,
                                     am_heap_t *src_heap,
                                     am_heap_t *dst_heap) {
    if (!ctx || !vm_alloc || !heap_alloc || !src_heap || !dst_heap) return -1;
    ctx->vm_alloc = vm_alloc;
    ctx->heap_alloc = heap_alloc;
    ctx->src_heap = src_heap;
    ctx->dst_heap = dst_heap;
    ctx->entries = NULL;
    ctx->count = 0;
    ctx->capacity = 0;
    return 0;
}


static void am_fork_heap_ctx_destroy(am_fork_heap_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->entries && ctx->vm_alloc) {
        am_free(ctx->vm_alloc, ctx->entries);
    }
    ctx->entries = NULL;
    ctx->count = 0;
    ctx->capacity = 0;
}


static am_handle_t am_fork_heap_map_handle(am_fork_heap_ctx_t *ctx, am_handle_t old_handle) {
    if (old_handle == AM_HANDLE_NULL) return AM_HANDLE_NULL;
    for (size_t i = 0; i < ctx->count; i++) {
        if (ctx->entries[i].old_handle == old_handle) {
            return ctx->entries[i].new_handle;
        }
    }
    return AM_HANDLE_NULL;
}


static am_value_t am_fork_heap_map_value(am_fork_heap_ctx_t *ctx, am_value_t old_value) {
    if (am_value_is_handle(old_value)) {
        am_handle_t new_handle = am_fork_heap_map_handle(ctx, am_value_to_handle(old_value));
        return am_make_value_of_handle(new_handle);
    }
    return old_value;
}


static void am_fork_heap_free_object(am_allocator_t *heap_alloc, am_object_t *obj) {
    if (!heap_alloc || !obj) return;
    switch (obj->type) {
        case AM_OBJECT_TYPE_LIST:         am_list_destroy(heap_alloc, (am_list_t *)obj); break;
        case AM_OBJECT_TYPE_MAP:          am_map_destroy(heap_alloc, (am_map_t *)obj); break;
        case AM_OBJECT_TYPE_WSTRING:      am_wstring_destroy(heap_alloc, (am_wstring_t *)obj); break;
        case AM_OBJECT_TYPE_CLOSURE:      am_closure_destroy(heap_alloc, (am_obj_closure_t *)obj); break;
        case AM_OBJECT_TYPE_CONTINUATION: am_continuation_destroy(heap_alloc, (am_continuation_t *)obj); break;
        default:                          am_free(heap_alloc, obj); break;
    }
}


static am_object_t *am_fork_heap_copy_object(am_fork_heap_ctx_t *ctx, am_object_t *old_obj) {
    if (!old_obj) return NULL;
    switch (old_obj->type) {
        case AM_OBJECT_TYPE_LIST:
            return (am_object_t *)am_list_copy(ctx->heap_alloc, (am_list_t *)old_obj);
        case AM_OBJECT_TYPE_MAP:
            // map 在第二遍重新插入，第一遍先分配空 map
            return (am_object_t *)am_map_create(ctx->heap_alloc, ((am_map_t *)old_obj)->capacity);
        case AM_OBJECT_TYPE_WSTRING:
            return (am_object_t *)am_wstring_copy(ctx->heap_alloc, (am_wstring_t *)old_obj);
        case AM_OBJECT_TYPE_CLOSURE:
            return (am_object_t *)am_closure_copy(ctx->heap_alloc, (am_obj_closure_t *)old_obj);
        case AM_OBJECT_TYPE_CONTINUATION:
            return (am_object_t *)am_continuation_copy(ctx->heap_alloc, (am_continuation_t *)old_obj);
        default:
            // 不支持的对象类型，深拷贝失败
            return NULL;
    }
}


static int32_t am_fork_heap_remap_object(am_fork_heap_ctx_t *ctx, size_t entry_index) {
    am_object_t *new_obj = ctx->entries[entry_index].new_obj;
    am_object_t *old_obj = ctx->entries[entry_index].old_obj;
    if (!new_obj || !old_obj) return 0;

    switch (new_obj->type) {
        case AM_OBJECT_TYPE_LIST: {
            am_list_t *lst = (am_list_t *)new_obj;
            for (size_t i = 0; i < lst->length; i++) {
                lst->children[i] = am_fork_heap_map_value(ctx, lst->children[i]);
            }
            lst->parent = am_fork_heap_map_handle(ctx, lst->parent);
            break;
        }
        case AM_OBJECT_TYPE_MAP: {
            am_map_t *src_m = (am_map_t *)old_obj;
            am_map_t *dst_m = (am_map_t *)new_obj;
            for (size_t i = 0; i < src_m->capacity; i++) {
                am_value_t k = src_m->slots[i].key;
                if (k == AM_MAP_KEY_EMPTY || k == AM_MAP_KEY_TOMBSTONE) continue;
                am_value_t new_k = am_fork_heap_map_value(ctx, k);
                am_value_t new_v = am_fork_heap_map_value(ctx, src_m->slots[i].value);
                am_map_t *new_m = am_map_set(ctx->heap_alloc, dst_m, new_k, new_v);
                if (!new_m) return -1;
                if (new_m != dst_m) {
                    am_handle_t hd = ctx->entries[entry_index].new_handle;
                    if (am_heap_set(ctx->vm_alloc, ctx->heap_alloc, ctx->dst_heap, hd,
                                    am_make_value_of_ptr((am_object_t *)new_m)) != 0) {
                        return -1;
                    }
                    dst_m = new_m;
                    ctx->entries[entry_index].new_obj = (am_object_t *)new_m;
                }
            }
            break;
        }
        case AM_OBJECT_TYPE_CLOSURE: {
            am_obj_closure_t *closure = (am_obj_closure_t *)new_obj;
            closure->parent = am_fork_heap_map_handle(ctx, closure->parent);
            for (size_t i = 0; i < closure->length; i++) {
                closure->bindings[i].value = am_fork_heap_map_value(ctx, closure->bindings[i].value);
            }
            break;
        }
        case AM_OBJECT_TYPE_CONTINUATION: {
            am_continuation_t *cont = (am_continuation_t *)new_obj;
            cont->current_closure_handle = am_fork_heap_map_handle(ctx, cont->current_closure_handle);
            for (size_t i = 0; i < cont->length; i++) {
                cont->stacks[i] = am_fork_heap_map_value(ctx, cont->stacks[i]);
            }
            break;
        }
        case AM_OBJECT_TYPE_WSTRING:
            // 字符串不含把柄引用，无需重映射
            break;
        default:
            // 不应到达：拷贝阶段已过滤不支持的类型
            return -1;
    }
    return 0;
}


static int32_t am_fork_heap_deep_copy(am_fork_heap_ctx_t *ctx) {
    if (!ctx || !ctx->src_heap || !ctx->src_heap->table) return -1;

    size_t src_count = am_map_length(ctx->vm_alloc, ctx->src_heap->table);
    am_value_t *keys = am_map_keys(ctx->vm_alloc, ctx->src_heap->table);
    if (!keys && src_count > 0) return -1;

    // 第一遍：为源堆中每个 handle 分配新 handle，并深拷贝对象（map 先分配空壳）
    for (size_t i = 0; i < src_count; i++) {
        am_handle_t old_handle = am_value_to_handle(keys[i]);
        am_value_t old_value = am_heap_get(ctx->vm_alloc, ctx->heap_alloc, ctx->src_heap, old_handle);
        am_object_t *old_obj = am_value_is_ptr(old_value) ? am_value_to_ptr(old_value) : NULL;

        am_handle_t new_handle = am_heap_alloc_handle(ctx->vm_alloc, ctx->heap_alloc, ctx->dst_heap);
        if (new_handle == AM_HANDLE_NULL) {
            free(keys);
            return -1;
        }

        am_object_t *new_obj = NULL;
        am_value_t new_value = old_value;
        if (old_obj) {
            new_obj = am_fork_heap_copy_object(ctx, old_obj);
            if (!new_obj) {
                free(keys);
                return -1;
            }
            new_value = am_make_value_of_ptr(new_obj);
        }

        if (am_heap_set(ctx->vm_alloc, ctx->heap_alloc, ctx->dst_heap, new_handle, new_value) != 0) {
            if (new_obj) am_fork_heap_free_object(ctx->heap_alloc, new_obj);
            free(keys);
            return -1;
        }

        if (ctx->count >= ctx->capacity) {
            size_t new_capacity = ctx->capacity ? ctx->capacity * 2 : 16;
            am_fork_heap_mapping_t *new_entries = (am_fork_heap_mapping_t *)am_realloc(
                ctx->vm_alloc, ctx->entries, new_capacity * sizeof(am_fork_heap_mapping_t));
            if (!new_entries) {
                free(keys);
                return -1;
            }
            ctx->entries = new_entries;
            ctx->capacity = new_capacity;
        }

        ctx->entries[ctx->count].old_handle = old_handle;
        ctx->entries[ctx->count].new_handle = new_handle;
        ctx->entries[ctx->count].old_obj = old_obj;
        ctx->entries[ctx->count].new_obj = new_obj;
        ctx->count++;
    }
    free(keys);

    // 第二遍：重映射各对象内部的把柄引用
    for (size_t i = 0; i < ctx->count; i++) {
        if (am_fork_heap_remap_object(ctx, i) != 0) {
            return -1;
        }
    }

    return 0;
}


static am_process_t *am_fork_process_copy(am_runtime_t *rt, am_process_t *parent) {
    (void)rt;
    if (!parent || !parent->vm_alloc || !parent->heap_alloc) return NULL;

    am_allocator_t *vm_alloc = parent->vm_alloc;
    am_allocator_t *heap_alloc = parent->heap_alloc;

    am_process_t *child = (am_process_t *)am_calloc(vm_alloc, sizeof(am_process_t));
    if (!child) return NULL;

    child->base = parent->base;
    child->vm_alloc = vm_alloc;
    child->heap_alloc = heap_alloc;
    child->pid = 0;
    child->parent_pid = parent->pid;
    child->state = AM_PROCESS_STATE_READY;
    child->PC = parent->PC;
    child->ilcode_length = parent->ilcode_length;
    child->current_closure_handle = parent->current_closure_handle;

    child->ilcode = (am_instruction_t *)am_malloc(vm_alloc, parent->ilcode_length * sizeof(am_instruction_t));
    if (!child->ilcode) goto fail;
    memcpy(child->ilcode, parent->ilcode, parent->ilcode_length * sizeof(am_instruction_t));

    child->var_vocab = am_vocab_copy(vm_alloc, parent->var_vocab);
    child->symbol_vocab = am_vocab_copy(vm_alloc, parent->symbol_vocab);
    child->var_type = am_list_copy(vm_alloc, parent->var_type);
    child->natives = am_map_copy(vm_alloc, parent->natives);
    if (!child->var_vocab || !child->symbol_vocab || !child->var_type || !child->natives) goto fail;

    child->opstack_capacity = parent->opstack_capacity;
    child->opstack = (am_value_t *)am_calloc(vm_alloc, child->opstack_capacity * sizeof(am_value_t));
    if (!child->opstack) goto fail;
    size_t opstack_len = am_process_length_of_opstack(parent);
    if (opstack_len != SIZE_MAX) {
        memcpy(child->opstack, parent->opstack, opstack_len * sizeof(am_value_t));
        child->opstack_top = child->opstack + opstack_len;
    } else {
        child->opstack_top = child->opstack;
    }

    child->fstack_capacity = parent->fstack_capacity;
    child->fstack = (am_value_t *)am_calloc(vm_alloc, child->fstack_capacity * sizeof(am_value_t));
    if (!child->fstack) goto fail;
    size_t fstack_len = am_process_length_of_fstack(parent);
    if (fstack_len != SIZE_MAX) {
        memcpy(child->fstack, parent->fstack, fstack_len * sizeof(am_value_t));
        child->fstack_top = child->fstack + fstack_len;
    } else {
        child->fstack_top = child->fstack;
    }

    size_t heap_capacity = parent->heap ? parent->heap->capacity : 16;
    child->heap = am_heap_create(vm_alloc, heap_alloc, heap_capacity);
    if (!child->heap) goto fail;

    am_fork_heap_ctx_t ctx;
    if (am_fork_heap_ctx_init(&ctx, vm_alloc, heap_alloc, parent->heap, child->heap) != 0) goto fail;

    int32_t copy_ok = am_fork_heap_deep_copy(&ctx);
    if (copy_ok == 0) {
        child->current_closure_handle = am_fork_heap_map_handle(&ctx, child->current_closure_handle);

        // 重映射 IL 指令中的字面 handle（如字符串字面量）
        for (size_t i = 0; i < child->ilcode_length; i++) {
            am_value_t operand = child->ilcode[i].operand;
            if (am_value_is_handle(operand)) {
                child->ilcode[i].operand = am_make_value_of_handle(
                    am_fork_heap_map_handle(&ctx, am_value_to_handle(operand)));
            }
        }

        size_t op_len = (size_t)(child->opstack_top - child->opstack);
        for (size_t i = 0; i < op_len; i++) {
            child->opstack[i] = am_fork_heap_map_value(&ctx, child->opstack[i]);
        }

        size_t f_len = (size_t)(child->fstack_top - child->fstack);
        for (size_t i = 0; i < f_len; i++) {
            child->fstack[i] = am_fork_heap_map_value(&ctx, child->fstack[i]);
        }
    }
    am_fork_heap_ctx_destroy(&ctx);

    if (copy_ok != 0) goto fail;

    return child;

fail:
    if (child) {
        am_process_destroy(child);
    }
    return NULL;
}


static am_pid_t am_fork_runtime_add_process(am_runtime_t *rt, am_process_t *proc) {
    if (!rt || !proc) return (am_pid_t)-1;

    am_pid_t pid = rt->process_poll_counter;
    if (pid >= rt->process_pool_capacity) {
        size_t new_cap = rt->process_pool_capacity * 2;
        am_process_t **new_pool = (am_process_t **)am_realloc(
            rt->vm_alloc, rt->process_pool, new_cap * sizeof(am_process_t *));
        if (!new_pool) return (am_pid_t)-1;
        rt->process_pool = new_pool;
        rt->process_pool_capacity = new_cap;
    }

    proc->pid = pid;
    rt->process_pool[pid] = proc;
    rt->process_poll_counter++;
    return pid;
}


// ===============================================================================
// Native 函数实现
// ===============================================================================

// (System.exec cmd:String) : String
// 同步执行 shell 命令，返回标准输出字符串。
int32_t am_native_System_exec(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    char *cmd = NULL;
    if (!native_pop_wstring_as_mb(proc, &cmd)) return -1;

    FILE *fp = popen(cmd, "r");
    am_free(proc->heap_alloc, cmd);
    if (!fp) {
        // 执行失败：返回空字符串
        return native_push_wstring_buf(proc, L"", 0);
    }

    // 读取标准输出
    size_t cap = 256;
    size_t len = 0;
    char *out = (char *)am_malloc(proc->heap_alloc, cap);
    if (!out) {
        pclose(fp);
        return -1;
    }

    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (len + n + 1 > cap) {
            size_t new_cap = cap * 2;
            while (len + n + 1 > new_cap) new_cap *= 2;
            char *new_out = (char *)am_malloc(proc->heap_alloc, new_cap);
            if (!new_out) {
                am_free(proc->heap_alloc, out);
                pclose(fp);
                return -1;
            }
            memcpy(new_out, out, len);
            am_free(proc->heap_alloc, out);
            out = new_out;
            cap = new_cap;
        }
        memcpy(out + len, buf, n);
        len += n;
    }

    int status = pclose(fp);
    if (status != 0 && len == 0) {
        // 命令执行出错且无输出：返回空字符串
        am_free(proc->heap_alloc, out);
        return native_push_wstring_buf(proc, L"", 0);
    }

    // 转换为宽字符串
    size_t wlen = mbstowcs(NULL, out, 0);
    if (wlen == (size_t)-1) {
        am_free(proc->heap_alloc, out);
        return native_push_wstring_buf(proc, L"", 0);
    }

    wchar_t *wout = (wchar_t *)am_malloc(proc->heap_alloc, (wlen + 1) * sizeof(wchar_t));
    if (!wout) {
        am_free(proc->heap_alloc, out);
        return -1;
    }
    mbstowcs(wout, out, wlen + 1);
    wout[wlen] = L'\0';
    am_free(proc->heap_alloc, out);

    int32_t ret = native_push_wstring_buf(proc, wout, wlen);
    am_free(proc->heap_alloc, wout);
    return ret;
}


// (System.set_timeout time_ms:Number callback:(void->undefined)) : Number(计时器编号)
int32_t am_native_System_set_timeout(am_runtime_t *rt, am_process_t *proc) {
    am_float_t time_ms;
    am_handle_t callback;

    // 参数退栈顺序与参数列表顺序相反：先 callback，后 time_ms
    if (!native_pop_closure(proc, &callback)) return -1;
    if (!native_pop_number(proc, &time_ms)) return -1;

    if (isnan(time_ms) || time_ms < 0) {
        // 非法延时：返回 0 计时器编号
        return native_push_float_or_null(proc, 0.0);
    }

    // 防止回调闭包被 GC 回收
    native_keepalive_closure(proc, callback);

    am_timestamp_t delay = (am_timestamp_t)time_ms;
    size_t timer_id = am_runtime_set_timer(rt, proc->pid, callback, delay, false, 0);
    if (timer_id == 0) return -1;

    return native_push_float_or_null(proc, (am_float_t)timer_id);
}


// (System.set_interval time_ms:Number callback:(void->undefined)) : Number(计时器编号)
int32_t am_native_System_set_interval(am_runtime_t *rt, am_process_t *proc) {
    am_float_t time_ms;
    am_handle_t callback;

    // 参数退栈顺序与参数列表顺序相反：先 callback，后 time_ms
    if (!native_pop_closure(proc, &callback)) return -1;
    if (!native_pop_number(proc, &time_ms)) return -1;

    if (isnan(time_ms) || time_ms < 0) {
        return native_push_float_or_null(proc, 0.0);
    }

    native_keepalive_closure(proc, callback);

    am_timestamp_t interval = (am_timestamp_t)time_ms;
    size_t timer_id = am_runtime_set_timer(rt, proc->pid, callback, interval, true, interval);
    if (timer_id == 0) return -1;

    return native_push_float_or_null(proc, (am_float_t)timer_id);
}


// (System.clear_timeout timer:Number) : void
int32_t am_native_System_clear_timeout(am_runtime_t *rt, am_process_t *proc) {
    am_float_t timer_id_f;
    if (!native_pop_number(proc, &timer_id_f)) return -1;

    if (!isnan(timer_id_f)) {
        am_runtime_clear_timer(rt, (size_t)timer_id_f);
    }

    am_process_step(proc);
    return 0;
}


// (System.clear_interval timer:Number) : void
int32_t am_native_System_clear_interval(am_runtime_t *rt, am_process_t *proc) {
    am_float_t timer_id_f;
    if (!native_pop_number(proc, &timer_id_f)) return -1;

    if (!isnan(timer_id_f)) {
        am_runtime_clear_timer(rt, (size_t)timer_id_f);
    }

    am_process_step(proc);
    return 0;
}


// (System.timestamp) : Number
int32_t am_native_System_timestamp(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_timestamp_t now = am_runtime_now_ms();
    return native_push_float_or_null(proc, (am_float_t)now);
}


// (System.memstat) : List
// 返回一个包含四项的列表：'(vm_capacity vm_used heap_capacity heap_used)，单位 bytes。
int32_t am_native_System_memstat(am_runtime_t *rt, am_process_t *proc) {
    if (!rt || !proc || !proc->heap) return -1;

    am_runtime_memory_stats_t stats;
    if (am_runtime_get_memory_stats(rt, &stats) != 0) return -1;

    am_list_t *lst = am_list_create(proc->heap_alloc, 4, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    if (!lst) return -1;

    lst = am_list_push(proc->heap_alloc, lst, am_make_value_of_uint((am_uint_t)stats.vm_capacity));
    if (!lst) return -1;
    lst = am_list_push(proc->heap_alloc, lst, am_make_value_of_uint((am_uint_t)stats.vm_used));
    if (!lst) return -1;
    lst = am_list_push(proc->heap_alloc, lst, am_make_value_of_uint((am_uint_t)stats.heap_capacity));
    if (!lst) return -1;
    lst = am_list_push(proc->heap_alloc, lst, am_make_value_of_uint((am_uint_t)stats.heap_used));
    if (!lst) return -1;

    am_handle_t hd = am_heap_alloc_handle(proc->vm_alloc, proc->heap_alloc, proc->heap);
    if (hd == AM_HANDLE_NULL) return -1;

    if (am_heap_set(proc->vm_alloc, proc->heap_alloc, proc->heap, hd,
                    am_make_value_of_ptr((am_object_t *)lst)) != 0) {
        am_heap_free_handle(proc->vm_alloc, proc->heap_alloc, proc->heap, hd);
        return -1;
    }

    if (am_process_push_operand(proc, am_make_value_of_handle(hd)) != 0) return -1;
    am_process_step(proc);
    return 0;
}


// (System.fork) : Number
// 深度复制当前进程，保留除 pid 和 parent_pid 外的全部状态。
// 亲进程返回子进程 pid，子进程返回 0。
int32_t am_native_System_fork(am_runtime_t *rt, am_process_t *proc) {
    if (!rt || !proc) return -1;

    // 亲进程 PC 前进到 fork 调用后的下一条指令
    am_process_step(proc);

    // 深拷贝子进程
    am_process_t *child = am_fork_process_copy(rt, proc);
    if (!child) return -1;

    // 子进程操作数栈压入 0
    if (am_process_push_operand(child, am_make_value_of_int(0)) != 0) {
        am_process_destroy(child);
        return -1;
    }
    // 将子进程加入运行时
    am_pid_t child_pid = am_fork_runtime_add_process(rt, child);
    if (child_pid == (am_pid_t)-1) {
        am_process_destroy(child);
        return -1;
    }
    // 亲进程操作数栈压入子进程 pid
    if (am_process_push_operand(proc, am_make_value_of_int((am_int_t)child_pid)) != 0) {
        return -1;
    }

    // 释放亲进程 CPU，将亲进程和子进程都加入调度队列
    am_process_set_state(proc, AM_PROCESS_STATE_READY);
    am_list_t *new_queue = am_list_push(rt->vm_alloc, rt->process_queue,
                                        am_make_value_of_uint((am_uint_t)proc->pid));
    if (!new_queue) {
        return -1;
    }
    rt->process_queue = new_queue;

    new_queue = am_list_push(rt->vm_alloc, rt->process_queue,
                             am_make_value_of_uint((am_uint_t)child_pid));
    if (!new_queue) {
        return -1;
    }
    rt->process_queue = new_queue;

    return 0;
}


// (System.test x:any) : String
// 保留的测试函数，用于兼容现有测试用例 mob.scm。
int32_t am_native_System_test(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_value_t v = am_process_pop_operand(proc);
    wchar_t buf[128];
    swprintf(buf, 128, L"Value=%zu", (size_t)v);
    return native_push_wstring_buf(proc, buf, wcslen(buf));
}


static const am_native_func_entry_t am_native_System_funcs[] = {
    { L"exec",         am_native_System_exec },
    { L"set_timeout",  am_native_System_set_timeout },
    { L"set_interval", am_native_System_set_interval },
    { L"clear_timeout",am_native_System_clear_timeout },
    { L"clear_interval",am_native_System_clear_interval },
    { L"timestamp",    am_native_System_timestamp },
    { L"memstat",      am_native_System_memstat },
    { L"fork",         am_native_System_fork },
    { L"test",         am_native_System_test },
};

const am_native_lib_entry_t am_native_System_lib = {
    L"System",
    am_native_System_funcs,
    sizeof(am_native_System_funcs) / sizeof(am_native_System_funcs[0])
};
