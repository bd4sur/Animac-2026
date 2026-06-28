#include "native.h"
#include "native_Math.h"
#include "wstring.h"

#include <wchar.h>


// 已注册的 native 库表。
// 新增库时，只需在其对应的头文件中暴露库表项，并在此数组中加入引用。
static const am_native_lib_entry_t *am_native_libs[] = {
    &am_native_System_lib,
    &am_native_Math_lib,
};
static const size_t am_native_lib_count =
    sizeof(am_native_libs) / sizeof(am_native_libs[0]);


// 运行时查表：根据库名和函数名定位native函数实现。
am_native_func_t am_native_find_func(const wchar_t *lib_name, const wchar_t *func_name) {
    if (!lib_name || !func_name) return NULL;

    for (size_t i = 0; i < am_native_lib_count; i++) {
        const am_native_lib_entry_t *lib = am_native_libs[i];
        if (!lib) continue;
        if (wcscmp(lib->name, lib_name) != 0) continue;

        for (size_t j = 0; j < lib->func_count; j++) {
            if (wcscmp(lib->funcs[j].name, func_name) == 0) {
                return lib->funcs[j].func;
            }
        }
    }
    return NULL;
}


////////////////////////////////////////////////////////////////////////////
//  Native Library : System
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


static const am_native_func_entry_t am_native_System_funcs[] = {
    { L"test", am_native_System_test },
};

const am_native_lib_entry_t am_native_System_lib = {
    L"System",
    am_native_System_funcs,
    sizeof(am_native_System_funcs) / sizeof(am_native_System_funcs[0])
};
