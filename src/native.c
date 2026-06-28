#include "native.h"
#include "wstring.h"

#include <wchar.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>


#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif


// 函数表项：库内的单个函数
typedef struct {
    const wchar_t *name; // 函数名（suffix）
    am_native_func_t func;
} am_native_func_entry_t;

// 库表项：一个native库及其函数表
typedef struct {
    const wchar_t *name;                  // 库名（prefix / native_id）
    const am_native_func_entry_t *funcs;  // 该库的函数表
    size_t func_count;                    // 函数表长度
} am_native_lib_entry_t;


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

// 将 float 结果压回操作数栈；若结果为 NaN，则压入 null。
static int32_t native_push_result(am_process_t *proc, am_float_t result) {
    if (isnan(result)) {
        if (am_process_push_operand(proc, AM_VALUE_NULL) != 0) return -1;
    }
    else {
        if (am_process_push_operand(proc, am_make_value_of_float(result)) != 0) return -1;
    }
    am_process_step(proc);
    return 0;
}


////////////////////////////////////////////////////////////////////////////
//  Native Library : System
////////////////////////////////////////////////////////////////////////////

static const am_native_func_entry_t am_nlib_System_funcs[] = {
    { L"test", am_native_System_test },
};


////////////////////////////////////////////////////////////////////////////
//  Native Library : Math
////////////////////////////////////////////////////////////////////////////

int32_t am_native_Math_PI(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    return native_push_result(proc, (am_float_t)M_PI);
}

int32_t am_native_Math_pow(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t exponent, base;
    if (!native_pop_number(proc, &exponent)) return -1;
    if (!native_pop_number(proc, &base)) return -1;
    return native_push_result(proc, pow(base, exponent));
}

int32_t am_native_Math_sqrt(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, sqrt(x));
}

int32_t am_native_Math_exp(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, exp(x));
}

int32_t am_native_Math_log(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, log(x));
}

int32_t am_native_Math_log10(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, log10(x));
}

int32_t am_native_Math_log2(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, log2(x));
}

int32_t am_native_Math_sin(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, sin(x));
}

int32_t am_native_Math_cos(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, cos(x));
}

int32_t am_native_Math_tan(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, tan(x));
}

int32_t am_native_Math_atan(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, atan(x));
}

int32_t am_native_Math_floor(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, floor(x));
}

int32_t am_native_Math_ceil(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, ceil(x));
}

int32_t am_native_Math_round(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, round(x));
}

int32_t am_native_Math_to_fixed(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t n, x;
    if (!native_pop_number(proc, &n)) return -1;
    if (!native_pop_number(proc, &x)) return -1;

    // 小数位数限制在合理范围
    int32_t digits = (int32_t)n;
    if (digits < 0) digits = 0;
    if (digits > 15) digits = 15;

    am_float_t factor = pow((am_float_t)10.0, (am_float_t)digits);
    am_float_t result = round(x * factor) / factor;
    return native_push_result(proc, result);
}

int32_t am_native_Math_abs(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_float_t x;
    if (!native_pop_number(proc, &x)) return -1;
    return native_push_result(proc, fabs(x));
}

int32_t am_native_Math_random(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = true;
    }
    am_float_t result = (am_float_t)rand() / ((am_float_t)RAND_MAX + (am_float_t)1.0);
    return native_push_result(proc, result);
}


static const am_native_func_entry_t am_nlib_Math_funcs[] = {
    { L"PI",       am_native_Math_PI },
    { L"pow",      am_native_Math_pow },
    { L"sqrt",     am_native_Math_sqrt },
    { L"exp",      am_native_Math_exp },
    { L"log",      am_native_Math_log },
    { L"log10",    am_native_Math_log10 },
    { L"log2",     am_native_Math_log2 },
    { L"sin",      am_native_Math_sin },
    { L"cos",      am_native_Math_cos },
    { L"tan",      am_native_Math_tan },
    { L"atan",     am_native_Math_atan },
    { L"floor",    am_native_Math_floor },
    { L"ceil",     am_native_Math_ceil },
    { L"round",    am_native_Math_round },
    { L"to_fixed", am_native_Math_to_fixed },
    { L"abs",      am_native_Math_abs },
    { L"random",   am_native_Math_random },
};


// Native 库注册表
static const am_native_lib_entry_t am_native_libs[] = {
    { L"System", am_nlib_System_funcs, sizeof(am_nlib_System_funcs) / sizeof(am_nlib_System_funcs[0]) },
    { L"Math",   am_nlib_Math_funcs,   sizeof(am_nlib_Math_funcs)   / sizeof(am_nlib_Math_funcs[0])   },
};


// 运行时查表：根据库名和函数名定位native函数实现。
am_native_func_t am_native_find_func(const wchar_t *lib_name, const wchar_t *func_name) {
    if (!lib_name || !func_name) return NULL;

    size_t lib_count = sizeof(am_native_libs) / sizeof(am_native_libs[0]);
    for (size_t i = 0; i < lib_count; i++) {
        if (wcscmp(am_native_libs[i].name, lib_name) != 0) continue;

        for (size_t j = 0; j < am_native_libs[i].func_count; j++) {
            if (wcscmp(am_native_libs[i].funcs[j].name, func_name) == 0) {
                return am_native_libs[i].funcs[j].func;
            }
        }
    }
    return NULL;
}


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////


// Native函数命名规范：am_native_<LibID>_<funcName>，对应LibID.funcName。
int32_t am_native_System_test(am_runtime_t *rt, am_process_t *proc) {
    (void)rt;
    am_value_t v = am_process_pop_operand(proc);
    wchar_t buf[128];
    swprintf(buf, 128, L"Value=%zu", v);
    am_wstring_t *ws = am_wstring_create(proc->heap_alloc, buf, wcslen(buf));
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
