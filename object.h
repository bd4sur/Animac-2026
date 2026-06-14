#ifndef __AM_OBJECT_H__
#define __AM_OBJECT_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct am_list_t;
typedef struct am_list_t am_list_t;

struct am_map_t;
typedef struct am_map_t am_map_t;

struct am_array_t;
typedef struct am_array_t am_array_t;

struct am_heap_t;
typedef struct am_heap_t am_heap_t;



///////////////////////////////////////////
// 对象语言数据值：TPV (Tagged Pointer Value)
///////////////////////////////////////////

// 与架构相关的基本类型
#if UINTPTR_MAX == 0xFFFFFFFF
    // 32 位系统
    typedef int32_t  am_int_t;
    typedef uint32_t am_uint_t;
    typedef float    am_float_t;
    typedef uint32_t am_float_bits_t;
    typedef size_t   am_symbol_t;
    typedef size_t   am_iaddr_t;
    typedef size_t   am_handle_t;
    typedef size_t   am_varid_t;
#elif UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
    // 64 位系统
    typedef int64_t  am_int_t;
    typedef uint64_t am_uint_t;
    typedef double   am_float_t;
    typedef uint64_t am_float_bits_t;
    typedef size_t   am_symbol_t;
    typedef size_t   am_iaddr_t;
    typedef size_t   am_handle_t;
    typedef size_t   am_varid_t;
#else
    #error "Only 32-bit and 64-bit architectures are supported."
#endif


// 与架构无关的基本类型
typedef bool am_boolean_t;
typedef uint32_t am_wchar_t;
typedef uint8_t am_undefined_t;
typedef uint8_t am_null_t;


// TPV(Tagged Pointer Value)作为唯一的值类型
typedef uintptr_t am_value_t;



// TPV的类型枚举
#define AM_VALUE_TYPE_PTR (0x00)
// 以下均为IMME
#define AM_VALUE_TYPE_HANDLE    (0x01) // uint_like
#define AM_VALUE_TYPE_IADDR     (0x02) // uint_like
#define AM_VALUE_TYPE_VARID     (0x03) // uint_like
#define AM_VALUE_TYPE_BOOLEAN   (0x04) // uint_like
#define AM_VALUE_TYPE_NULL      (0x05) // uint_like, 单例
#define AM_VALUE_TYPE_UNDEFINED (0x06) // uint_like, 单例
#define AM_VALUE_TYPE_SYMBOL    (0x07) // uint_like, keyword也是一种特殊的symbol，在编译时就应该放进symbol映射表中
#define AM_VALUE_TYPE_WCHAR     (0x08) // wchar_t, 仅用于组成字符串
#define AM_VALUE_TYPE_UINT      (0x09) // number
#define AM_VALUE_TYPE_INT       (0x0A) // number
#define AM_VALUE_TYPE_FLOAT     (0x0B) // number


// TPV的类型标记：征用 Bit0 （从LSB开始）作为 Tag
#define AM_VALUE_TAG_MASK  ((am_value_t)0x1ULL)
#define AM_VALUE_TAG_PTR   ((am_value_t)0x0ULL) // 0: 指向堆上对象的指针
#define AM_VALUE_TAG_IMME  ((am_value_t)0x1ULL) // 1: 立即数 (小整数)

// TPV的立即数类型标记 (占用Bits1-4，从LSB开始)
// TODO 改成移位之后的值似乎更好
#define AM_VALUE_TAG_IMME_MASK      ((am_value_t)0x1EULL)
#define AM_VALUE_TAG_IMME_HANDLE    ((am_value_t)0x1ULL)
#define AM_VALUE_TAG_IMME_IADDR     ((am_value_t)0x2ULL)
#define AM_VALUE_TAG_IMME_VARID     ((am_value_t)0x3ULL)
#define AM_VALUE_TAG_IMME_BOOLEAN   ((am_value_t)0x4ULL)
#define AM_VALUE_TAG_IMME_NULL      ((am_value_t)0x5ULL)
#define AM_VALUE_TAG_IMME_UNDEFINED ((am_value_t)0x6ULL)
#define AM_VALUE_TAG_IMME_SYMBOL    ((am_value_t)0x7ULL)
#define AM_VALUE_TAG_IMME_WCHAR     ((am_value_t)0x8ULL)  // 最少27bits，能装得下unicode全部码点
#define AM_VALUE_TAG_IMME_UINT      ((am_value_t)0x9ULL)
#define AM_VALUE_TAG_IMME_INT       ((am_value_t)0xAULL)
#define AM_VALUE_TAG_IMME_FLOAT     ((am_value_t)0xBULL)

// 方便构建uint_like的value
#define AM_MAKE_VALUE_OF_UINT_LIKE(x, imme_type_tag) ((am_value_t)(((am_value_t)(x) << 5) | ((imme_type_tag) << 1) | AM_VALUE_TAG_IMME))


///////////////////////////////////////////
// 特殊（单例）TPV
///////////////////////////////////////////

#define AM_VALUE_NULL      AM_MAKE_VALUE_OF_UINT_LIKE(0x0, AM_VALUE_TAG_IMME_NULL)
#define AM_VALUE_UNDEFINED AM_MAKE_VALUE_OF_UINT_LIKE(0x0, AM_VALUE_TAG_IMME_UNDEFINED)
#define AM_VALUE_TRUE      AM_MAKE_VALUE_OF_UINT_LIKE(0x1, AM_VALUE_TAG_IMME_BOOLEAN)
#define AM_VALUE_FALSE     AM_MAKE_VALUE_OF_UINT_LIKE(0x0, AM_VALUE_TAG_IMME_BOOLEAN)

// 首把柄和空把柄（值为(UINTPTR_MAX>>5)的把柄）
#define AM_VALUE_HANDLE_BASE  AM_MAKE_VALUE_OF_UINT_LIKE(0x0, AM_VALUE_TAG_IMME_HANDLE)
#define AM_VALUE_HANDLE_NULL  AM_MAKE_VALUE_OF_UINT_LIKE(UINTPTR_MAX, AM_VALUE_TAG_IMME_HANDLE)

// 关键字（保留的symbol）
// NOTE 关键字在词法上属于identifier，与variable接近；但是在语义上属于symbol，全局保留的symbol，不带前导单引号的特殊symbol
// lambda define set! let begin return ... _
// if and or cond else for while break continue case do
// quote quasiquote unquote
// import native
#define AM_VALUE_KW_lambda     AM_MAKE_VALUE_OF_UINT_LIKE(0x00, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_define     AM_MAKE_VALUE_OF_UINT_LIKE(0x01, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_set        AM_MAKE_VALUE_OF_UINT_LIKE(0x02, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_let        AM_MAKE_VALUE_OF_UINT_LIKE(0x03, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_begin      AM_MAKE_VALUE_OF_UINT_LIKE(0x04, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_return     AM_MAKE_VALUE_OF_UINT_LIKE(0x05, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_dot3       AM_MAKE_VALUE_OF_UINT_LIKE(0x06, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_underscore AM_MAKE_VALUE_OF_UINT_LIKE(0x07, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_if         AM_MAKE_VALUE_OF_UINT_LIKE(0x08, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_and        AM_MAKE_VALUE_OF_UINT_LIKE(0x09, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_or         AM_MAKE_VALUE_OF_UINT_LIKE(0x0A, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_cond       AM_MAKE_VALUE_OF_UINT_LIKE(0x0B, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_else       AM_MAKE_VALUE_OF_UINT_LIKE(0x0C, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_for        AM_MAKE_VALUE_OF_UINT_LIKE(0x0D, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_while      AM_MAKE_VALUE_OF_UINT_LIKE(0x0E, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_break      AM_MAKE_VALUE_OF_UINT_LIKE(0x0F, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_continue   AM_MAKE_VALUE_OF_UINT_LIKE(0x10, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_case       AM_MAKE_VALUE_OF_UINT_LIKE(0x11, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_do         AM_MAKE_VALUE_OF_UINT_LIKE(0x12, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_quote      AM_MAKE_VALUE_OF_UINT_LIKE(0x13, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_quasiquote AM_MAKE_VALUE_OF_UINT_LIKE(0x14, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_unquote    AM_MAKE_VALUE_OF_UINT_LIKE(0x15, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_import     AM_MAKE_VALUE_OF_UINT_LIKE(0x16, AM_VALUE_TAG_IMME_SYMBOL)
#define AM_VALUE_KW_native     AM_MAKE_VALUE_OF_UINT_LIKE(0x17, AM_VALUE_TAG_IMME_SYMBOL)





///////////////////////////////////////////
// 对象语言数据对象：Object
///////////////////////////////////////////


// Object类型枚举
#define AM_OBJECT_TYPE_BASE         (0x00)  // 默认类型（基类）
#define AM_OBJECT_TYPE_LIST         (0x01)  // 通用线性表List<am_value_t>
#define AM_OBJECT_TYPE_MAP          (0x02)  // 通用散列表Map<am_value_t, am_value_t>
#define AM_OBJECT_TYPE_STRING       (0x03)  // 字符串
#define AM_OBJECT_TYPE_PORT         (0x04)  // 端口（对IO的抽象）
#define AM_OBJECT_TYPE_CLOSURE      (0x05)  // 闭包
#define AM_OBJECT_TYPE_CONTINUATION (0x06)  // 续体
#define AM_OBJECT_TYPE_FRAME        (0x07)  // 栈帧
#define AM_OBJECT_TYPE_ILCODE       (0x08)  // 中间语言指令 TODO
#define AM_OBJECT_TYPE_BOX          (0x09)  // 基本类型装箱 TODO


// Object基类（公共头）
typedef struct am_object_t {
    uint32_t header; // TODO 预留，包括魔法值、static标记等
    uint32_t hash;   // T散列值
    uint32_t gcmark; // TODO 用于垃圾回收，具体用法待定，取决于垃圾回收算法
    int32_t  type;   // 对象类型（AM_OBJECT_TYPE_*）
} am_object_t;





///////////////////////////////////////////
// List对象
///////////////////////////////////////////

// List堆对象（am_obj_list_t作为对象语言的数据对象，实质上是am_list_t）
typedef am_list_t am_obj_list_t;



///////////////////////////////////////////
// Map对象
///////////////////////////////////////////

// Map堆对象（am_obj_map_t作为对象语言的数据对象，实质上是am_map_t）
typedef am_map_t am_obj_map_t;








// 续体类型
typedef struct am_obj_continuation_t {
    am_object_t base;

    am_value_t cont_return_target; // iaddr
    am_value_t current_closure_handle; // handle
    // am_obj_array_t *opstack; // Array<value>
    // am_obj_array_t *fstack;  // Array<value>
} am_obj_continuation_t;

// 栈帧类型
typedef struct am_obj_frame_t {
    am_object_t base;

    am_value_t handle_to_closure_object;
    am_value_t iaddr;
} am_obj_frame_t;





// TPV基本操作



// 提取立即数子类型标记
static inline am_value_t am_get_value_sub_tag(am_value_t v) { return (v & AM_VALUE_TAG_IMME_MASK) >> 1; }

// 类型谓词
static inline bool am_value_is_ptr(am_value_t v)       { return (v & AM_VALUE_TAG_MASK) == AM_VALUE_TAG_PTR; }
static inline bool am_value_is_imme(am_value_t v)      { return (v & AM_VALUE_TAG_MASK) == AM_VALUE_TAG_IMME; }
static inline bool am_value_is_handle(am_value_t v)    { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_HANDLE; }
static inline bool am_value_is_iaddr(am_value_t v)     { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_IADDR; }
static inline bool am_value_is_varid(am_value_t v)     { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_VARID; }
static inline bool am_value_is_boolean(am_value_t v)   { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_BOOLEAN; }
static inline bool am_value_is_null(am_value_t v)      { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_NULL; }
static inline bool am_value_is_undefined(am_value_t v) { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_UNDEFINED; }
static inline bool am_value_is_symbol(am_value_t v)    { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_SYMBOL; }
static inline bool am_value_is_wchar(am_value_t v)     { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_WCHAR; }
static inline bool am_value_is_uint(am_value_t v)      { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_UINT; }
static inline bool am_value_is_int(am_value_t v)       { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_INT; }
static inline bool am_value_is_float(am_value_t v)     { return am_value_is_imme(v) && am_get_value_sub_tag(v) == AM_VALUE_TAG_IMME_FLOAT; }
static inline bool am_value_is_number(am_value_t v)    { return am_value_is_float(v) || am_value_is_int(v) || am_value_is_uint(v); }

// 解包（不做类型检查，直接解包）
static inline am_object_t*   am_value_to_ptr(am_value_t v)       { return (am_object_t*)(v & ~AM_VALUE_TAG_MASK); }
static inline am_handle_t    am_value_to_handle(am_value_t v)    { return (am_handle_t)(v >> 5); }
static inline am_iaddr_t     am_value_to_iaddr(am_value_t v)     { return (am_iaddr_t)(v >> 5); }
static inline am_varid_t     am_value_to_varid(am_value_t v)     { return (am_varid_t)(v >> 5); }
static inline am_boolean_t   am_value_to_boolean(am_value_t v)   { return (am_boolean_t)(v >> 5); } // 整数部分非0即为#t，除此之外全部为#f
static inline am_null_t      am_value_to_null(am_value_t v)      { return (am_null_t)(1); } // 单例：常函数，且具体值不重要
static inline am_undefined_t am_value_to_undefined(am_value_t v) { return (am_undefined_t)(1); } // 单例：常函数，具体值不重要
static inline am_symbol_t    am_value_to_symbol(am_value_t v)    { return (am_symbol_t)(v >> 5); }
static inline am_wchar_t     am_value_to_wchar(am_value_t v)     { return (am_wchar_t)(v >> 5); }
static inline am_uint_t      am_value_to_uint(am_value_t v)      { return (am_uint_t)(v >> 5); }
static inline am_int_t am_value_to_int(am_value_t v) {
    am_value_t data = v >> 5; // 剥离类型标签
    am_int_t shifted = (am_int_t)(data << 5); // 跨平台符号扩展：推到最高位
    return shifted >> 5; // 算术右移恢复
}
static inline am_float_t am_value_to_float(am_value_t v) {
    uintptr_t data = v >> 5; // 剥离类型标签
    am_float_bits_t bits = (am_float_bits_t)(data << 5); // 左移 5 位恢复高位，低 5 位自动补 0
    am_float_t f;
    memcpy(&f, &bits, sizeof(am_float_t)); // 安全还原为浮点数
    return f;
}

// 打包
static inline am_value_t am_make_value_of_ptr(am_object_t* obj_p) { return (am_value_t)obj_p; }
static inline am_value_t am_make_value_of_handle(am_handle_t x) { return AM_MAKE_VALUE_OF_UINT_LIKE(x, AM_VALUE_TAG_IMME_HANDLE); }
static inline am_value_t am_make_value_of_iaddr(am_iaddr_t x) { return AM_MAKE_VALUE_OF_UINT_LIKE(x, AM_VALUE_TAG_IMME_IADDR); }
static inline am_value_t am_make_value_of_varid(am_varid_t x) { return AM_MAKE_VALUE_OF_UINT_LIKE(x, AM_VALUE_TAG_IMME_VARID); }
static inline am_value_t am_make_value_of_boolean(am_boolean_t x) { return AM_MAKE_VALUE_OF_UINT_LIKE(x, AM_VALUE_TAG_IMME_BOOLEAN); }
static inline am_value_t am_make_value_of_null(am_null_t x) { return AM_MAKE_VALUE_OF_UINT_LIKE(x, AM_VALUE_TAG_IMME_NULL); } // 单例：常函数，输入不重要
static inline am_value_t am_make_value_of_undefined(am_undefined_t x) { return AM_MAKE_VALUE_OF_UINT_LIKE(x, AM_VALUE_TAG_IMME_UNDEFINED); } // 单例：常函数，输入不重要
static inline am_value_t am_make_value_of_symbol(am_symbol_t x) { return AM_MAKE_VALUE_OF_UINT_LIKE(x, AM_VALUE_TAG_IMME_SYMBOL); }
static inline am_value_t am_make_value_of_wchar(am_wchar_t x) { return AM_MAKE_VALUE_OF_UINT_LIKE(x, AM_VALUE_TAG_IMME_WCHAR); }
static inline am_value_t am_make_value_of_uint(am_uint_t x) { return AM_MAKE_VALUE_OF_UINT_LIKE(x, AM_VALUE_TAG_IMME_UINT); }
static inline am_value_t am_make_value_of_int(am_int_t x) {
    am_value_t bits = (am_value_t)x;
    am_value_t shifted = bits << 5;
    return shifted | (AM_VALUE_TAG_IMME_INT << 1) | AM_VALUE_TAG_IMME;
}
static inline am_value_t am_make_value_of_float(am_float_t x) {
    am_float_bits_t bits;
    memcpy(&bits, &x, sizeof(am_float_t)); // 安全获取 IEEE754 位模式
    uintptr_t data = (uintptr_t)(bits >> 5); // 无论32位还是64位都截断低5位尾数，保留符号位和指数位
    return (data << 5) | (AM_VALUE_TAG_IMME_FLOAT << 1) | AM_VALUE_TAG_IMME; // 左移5位腾出类型标签，并填入
}








///////////////////////////////////////////
// TODO 虚拟机进程（其中包含进程内部的环境，例如堆、栈、代码、PC等等）
///////////////////////////////////////////

typedef void am_runtime_t;

typedef struct am_process_t {
    // TODO
    // ...
    am_runtime_t *runtime; // 指向全局运行时环境
    am_heap_t *heap;
    // ...
} am_process_t;



///////////////////////////////////////////
// TODO 虚拟机运行时环境
///////////////////////////////////////////

typedef void am_runtime_t;



#endif