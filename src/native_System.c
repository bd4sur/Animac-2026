#include "native_System.h"
#include "wstring.h"

#include <wchar.h>


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
