#include "native.h"
#include "native_System.h"
#include "native_Math.h"

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
