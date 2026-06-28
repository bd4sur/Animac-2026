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
    { L"test",         am_native_System_test },
};

const am_native_lib_entry_t am_native_System_lib = {
    L"System",
    am_native_System_funcs,
    sizeof(am_native_System_funcs) / sizeof(am_native_System_funcs[0])
};
