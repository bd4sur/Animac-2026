#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "object.h"
#include "opcode.h"
#include "ast.h"
#include "list.h"
#include "map.h"
#include "vocab.h"
#include "compiler.h"


// ===============================================================================
// 内部辅助函数
// ===============================================================================

// AST节点类型分类（用于编译器内部的节点类型判断）
#define AM_COMPILER_NODE_KIND_UNKNOWN   (-1)
#define AM_COMPILER_NODE_KIND_LAMBDA    (0)
#define AM_COMPILER_NODE_KIND_APPLICATION (1)
#define AM_COMPILER_NODE_KIND_QUOTE     (2)
#define AM_COMPILER_NODE_KIND_QUASIQUOTE (3)
#define AM_COMPILER_NODE_KIND_UNQUOTE   (4)
#define AM_COMPILER_NODE_KIND_STRING    (5)

// 编译器视角的值的类型分类
#define AM_COMPILER_VALUE_TYPE_HANDLE   (0)
#define AM_COMPILER_VALUE_TYPE_VARID    (1)
#define AM_COMPILER_VALUE_TYPE_SYMBOL   (2)
#define AM_COMPILER_VALUE_TYPE_NUMBER   (3)
#define AM_COMPILER_VALUE_TYPE_BOOLEAN  (4)
#define AM_COMPILER_VALUE_TYPE_NULL     (5)
#define AM_COMPILER_VALUE_TYPE_UNDEFINED (6)
#define AM_COMPILER_VALUE_TYPE_WCHAR    (8)
#define AM_COMPILER_VALUE_TYPE_OTHER    (9)


static int32_t compiler_value_type(am_value_t v) {
    if (am_value_is_handle(v))    return AM_COMPILER_VALUE_TYPE_HANDLE;
    if (am_value_is_varid(v))     return AM_COMPILER_VALUE_TYPE_VARID;
    if (am_value_is_symbol(v))    return AM_COMPILER_VALUE_TYPE_SYMBOL;
    if (am_value_is_number(v))    return AM_COMPILER_VALUE_TYPE_NUMBER;
    if (am_value_is_boolean(v))   return AM_COMPILER_VALUE_TYPE_BOOLEAN;
    if (am_value_is_null(v))      return AM_COMPILER_VALUE_TYPE_NULL;
    if (am_value_is_undefined(v)) return AM_COMPILER_VALUE_TYPE_UNDEFINED;
    if (am_value_is_wchar(v))     return AM_COMPILER_VALUE_TYPE_WCHAR;
    return AM_COMPILER_VALUE_TYPE_OTHER;
}


static int32_t compiler_node_kind(am_compiler_ctx_t *ctx, am_handle_t h) {
    am_value_t v = am_ast_get_node(ctx->ast, h);
    if (!am_value_is_ptr(v)) return AM_COMPILER_NODE_KIND_UNKNOWN;

    am_object_t *obj = am_value_to_ptr(v);
    if (obj->type == AM_OBJECT_TYPE_WSTRING) {
        return AM_COMPILER_NODE_KIND_STRING;
    }
    if (obj->type == AM_OBJECT_TYPE_LIST) {
        am_list_t *lst = (am_list_t *)obj;
        switch (lst->type) {
            case AM_LIST_TYPE_LAMBDA:      return AM_COMPILER_NODE_KIND_LAMBDA;
            case AM_LIST_TYPE_APPLICATION: return AM_COMPILER_NODE_KIND_APPLICATION;
            case AM_LIST_TYPE_QUOTE:       return AM_COMPILER_NODE_KIND_QUOTE;
            case AM_LIST_TYPE_QUASIQUOTE:  return AM_COMPILER_NODE_KIND_QUASIQUOTE;
            case AM_LIST_TYPE_UNQUOTE:     return AM_COMPILER_NODE_KIND_UNQUOTE;
            default: break;
        }
    }
    return AM_COMPILER_NODE_KIND_UNKNOWN;
}


static am_uint_t compiler_lambda_param_count(am_list_t *lambda) {
    if (!lambda || lambda->length < 2) return 0;
    am_value_t n = lambda->children[1];
    if (!am_value_is_uint(n)) return 0;
    return am_value_to_uint(n);
}


static int32_t compiler_is_tailcall(am_compiler_ctx_t *ctx, am_handle_t handle) {
    if (!ctx || !ctx->ast || !ctx->ast->tailcall_handles) return -1;
    size_t idx = am_list_find(ctx->ast->alloc, ctx->ast->tailcall_handles,
                               am_make_value_of_handle(handle), 0);
    return (idx != SIZE_MAX) ? 0 : -1;
}


static int32_t compiler_is_native_ref(am_compiler_ctx_t *ctx, am_value_t v) {
    if (!am_value_is_varid(v)) return -1;
    return am_ast_check_native_ref(ctx->ast, am_value_to_varid(v));
}


static int32_t compiler_primitive_opcode_for_varid(am_compiler_ctx_t *ctx, am_varid_t varid) {
    if (!ctx || !ctx->ast || !ctx->ast->var_vocab) return -1;
    wchar_t *name = am_vocab_get(ctx->ast->alloc, ctx->ast->var_vocab, &varid);
    if (!name) return -1;

    // 通过 AM_GLOBAL_BUILTIN_VAR 查找 builtin 下标，再通过 AM_BUILTIN_OPCODE_MAP 取得 opcode。
    // 这样 compiler 与 parser 对 builtin/primitive 的认知保持一致。
    for (size_t i = 0; i < AM_GLOBAL_BUILTIN_VAR_NUM; i++) {
        if (wcscmp(name, AM_GLOBAL_BUILTIN_VAR[i]) == 0) {
            return AM_BUILTIN_OPCODE_MAP[i];
        }
    }
    return -1;
}


static int32_t compiler_is_break_continue(am_value_t v, int *is_break) {
    if (!am_value_is_symbol(v)) return -1;
    am_symbol_t sym = am_value_to_symbol(v);
    if (sym == am_value_to_symbol(AM_VALUE_KW_break)) {
        if (is_break) *is_break = 1;
        return 0;
    }
    if (sym == am_value_to_symbol(AM_VALUE_KW_continue)) {
        if (is_break) *is_break = 0;
        return 0;
    }
    return -1;
}


static int32_t compiler_varid_name_equals(am_compiler_ctx_t *ctx, am_varid_t varid, const wchar_t *name) {
    if (!ctx || !ctx->ast || !ctx->ast->var_vocab) return -1;
    wchar_t *vname = am_vocab_get(ctx->ast->alloc, ctx->ast->var_vocab, &varid);
    if (!vname) return -1;
    return wcscmp(vname, name) == 0 ? 0 : -1;
}


// ===============================================================================
// while 标签栈操作
// ===============================================================================

static int32_t while_tag_stack_push(am_compiler_ctx_t *ctx, am_value_t cond_tag, am_value_t end_tag) {
    if (!ctx || !ctx->while_tag_stack) return -1;
    am_list_t *lst = am_list_push(ctx->ast->alloc, ctx->while_tag_stack, cond_tag);
    if (!lst) return -1;
    ctx->while_tag_stack = lst;
    lst = am_list_push(ctx->ast->alloc, ctx->while_tag_stack, end_tag);
    if (!lst) return -1;
    ctx->while_tag_stack = lst;
    return 0;
}


static int32_t while_tag_stack_top(am_compiler_ctx_t *ctx, am_value_t *cond_tag, am_value_t *end_tag) {
    if (!ctx || !ctx->while_tag_stack || ctx->while_tag_stack->length < 2) return -1;
    size_t len = ctx->while_tag_stack->length;
    if (cond_tag) *cond_tag = am_list_get(ctx->ast->alloc, ctx->while_tag_stack, len - 2);
    if (end_tag)  *end_tag  = am_list_get(ctx->ast->alloc, ctx->while_tag_stack, len - 1);
    return 0;
}


static int32_t while_tag_stack_pop(am_compiler_ctx_t *ctx) {
    if (!ctx || !ctx->while_tag_stack || ctx->while_tag_stack->length < 2) return -1;
    ctx->while_tag_stack->length -= 2;
    return 0;
}


// ===============================================================================
// 工具函数：指令添加、标签构造/定位/解析、临时变量
// ===============================================================================

// 功能说明：向am_compiler_ctx_t的ilcode中，增加一个am_instruction_t，并更新icount。
// 实现说明：成功返回0；失败返回-1
static int32_t emit_instruction(am_compiler_ctx_t *ctx, uint32_t opcode, am_value_t operand) {
    if (!ctx) return -1;
    am_instruction_t *new_ilcode = (am_instruction_t *)realloc(
        ctx->ilcode, (ctx->icount + 1) * sizeof(am_instruction_t));
    if (!new_ilcode) return -1;
    ctx->ilcode = new_ilcode;
    ctx->ilcode[ctx->icount].opcode = opcode;
    ctx->ilcode[ctx->icount].operand = operand;
    ctx->icount++;
    return 0;
}


// 功能说明：标签构造——根据给定的索引TPV（index_value），构造标签（am_value_t）。
// 实现说明：基于任意TPV（一般是handle、varid，称为“索引”TPV）构造一个新的标签TPV（AM_VALUE_TYPE_LABEL）。如果相同索引TPV的标签已存在，则获取已构造的标签TPV，以便后面加入指令的operand。由于编译过程中存在先使用后出现的情况，因此对于同一索引的标签，第一次调用本函数，是从无到有地创建标签，后续调用则是返回已创建的同一标签。只要用于构造标签的索引TPV相等，则构造出来的标签就是同一个标签，这种判定原则与symbol类似。成功返回标签TPV，失败返回AM_VALUE_NULL。
static am_value_t am_compiler_make_label(am_compiler_ctx_t *ctx, am_value_t index_value) {
    if (!ctx || !ctx->value_label_mapping) return AM_VALUE_NULL;

    am_value_t existing = am_map_get(ctx->ast->alloc, ctx->value_label_mapping, index_value);
    if (am_value_is_label(existing)) {
        return existing;
    }

    am_label_t new_label_id = ctx->label_counter++;
    am_value_t label = am_make_value_of_label(new_label_id);

    am_map_t *new_map = am_map_set(ctx->ast->alloc, ctx->value_label_mapping, index_value, label);
    if (!new_map) return AM_VALUE_NULL;
    ctx->value_label_mapping = new_map;
    return label;
}


// 功能说明：标签定位——为标签指定iaddr。
// 实现说明：标签的功能是指代指令序列中的位置。定位指的是将某个标签TPV与已知的iaddr（过去和当前的iaddr，不可能预知未来的iaddr）进行绑定，将标签->iaddr的映射关系，登记到label_iaddr_mapping中。编译过程中，标签的构造和定位，未必是同时发生的，但必须遵守先构造后定位的原则。成功返回0，失败返回-1。
static int32_t am_compiler_locate_label(am_compiler_ctx_t *ctx, am_value_t index_value, am_iaddr_t iaddr) {
    if (!ctx || !ctx->value_label_mapping || !ctx->label_iaddr_mapping) return -1;

    am_value_t label = am_map_get(ctx->ast->alloc, ctx->value_label_mapping, index_value);
    if (!am_value_is_label(label)) return -1;

    am_map_t *new_map = am_map_set(ctx->ast->alloc, ctx->label_iaddr_mapping,
                                    label, am_make_value_of_iaddr(iaddr));
    if (!new_map) return -1;
    ctx->label_iaddr_mapping = new_map;
    return 0;
}


// 功能说明：标签解析——通过标签TPV，获取对应的iaddr。
// 实现说明：在AST全部编译完成后，编译器收集到全部的label及其与iaddr的映射关系，此时即可通过label_iaddr_mapping，将所有的label解析并成绝对的iaddr。成功返回iaddr，失败返回SIZE_MAX。
static am_iaddr_t am_compiler_parse_label_to_iaddr(am_compiler_ctx_t *ctx, am_value_t label) {
    if (!ctx || !ctx->label_iaddr_mapping || !am_value_is_label(label)) return SIZE_MAX;

    am_value_t iaddr_val = am_map_get(ctx->ast->alloc, ctx->label_iaddr_mapping, label);
    if (!am_value_is_iaddr(iaddr_val)) return SIZE_MAX;
    return am_value_to_iaddr(iaddr_val);
}


// 功能说明：构造一个临时变量，加入AST，返回其varid；或者查询符合给定条件的临时变量的varid。
// 设计说明：编译过程中，某些结构需要引入临时变量，本函数即用于这类过程。
// 实现说明：成功返回varid，失败返回SIZE_MAX
static am_varid_t am_compiler_make_temp_varid(am_compiler_ctx_t *ctx, wchar_t *name, am_value_t label, size_t id) {
    if (!ctx || !ctx->ast || !name) return SIZE_MAX;

    wchar_t buf[256];
    int n = swprintf(buf, 256, L"%ls_%zx_%zx", name, (size_t)label, id);
    if (n <= 0 || (size_t)n >= 256) return SIZE_MAX;

    size_t existing = am_vocab_find(ctx->ast->alloc, ctx->ast->var_vocab, buf);
    if (existing != SIZE_MAX) {
        return (am_varid_t)existing;
    }

    size_t old_len = ctx->ast->var_vocab->length;
    size_t new_varid;
    ctx->ast->var_vocab = am_vocab_insert(ctx->ast->alloc, ctx->ast->var_vocab, buf, &new_varid);
    if (!ctx->ast->var_vocab || new_varid == SIZE_MAX) return SIZE_MAX;

    if (new_varid >= old_len) {
        am_list_t *vt = am_list_push(ctx->ast->alloc, ctx->ast->var_type,
                                      am_make_value_of_uint(AM_VAR_TYPE_ILTEMP));
        if (!vt) return SIZE_MAX;
        ctx->ast->var_type = vt;
    }
    else {
        am_list_set(ctx->ast->alloc, ctx->ast->var_type, new_varid,
                    am_make_value_of_uint(AM_VAR_TYPE_ILTEMP));
    }

    return (am_varid_t)new_varid;
}


// ===============================================================================
// 前向声明
// ===============================================================================

static int32_t compile_value(am_compiler_ctx_t *ctx, am_value_t v);
static int32_t compile_application(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_complex_application(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_lambda(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_callcc(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_begin(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_define(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_set(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_cond(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_if(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_while(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_and(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_or(am_compiler_ctx_t *ctx, am_handle_t handle);
static int32_t compile_quasiquote(am_compiler_ctx_t *ctx, am_handle_t handle);


// ===============================================================================
// 值编译
// ===============================================================================

// 编译条件表达式（predicate）：handle application需要求值，其余直接push或load
static int32_t compile_predicate(am_compiler_ctx_t *ctx, am_value_t v) {
    if (!ctx) return -1;

    int32_t vt = compiler_value_type(v);
    if (vt == AM_COMPILER_VALUE_TYPE_HANDLE) {
        int32_t kind = compiler_node_kind(ctx, am_value_to_handle(v));
        if (kind == AM_COMPILER_NODE_KIND_APPLICATION) {
            return compile_application(ctx, am_value_to_handle(v));
        }
        return emit_instruction(ctx, AM_VM_OP_push, v);
    }

    if (vt == AM_COMPILER_VALUE_TYPE_VARID) {
        if (compiler_is_native_ref(ctx, v) == 0) {
            return emit_instruction(ctx, AM_VM_OP_push, v);
        }
        return emit_instruction(ctx, AM_VM_OP_load, v);
    }

    if (vt == AM_COMPILER_VALUE_TYPE_SYMBOL) {
        int is_break;
        if (compiler_is_break_continue(v, &is_break) == 0) return -1; // predicate中不允许break/continue
        return emit_instruction(ctx, AM_VM_OP_push, v);
    }

    if (vt == AM_COMPILER_VALUE_TYPE_NUMBER ||
        vt == AM_COMPILER_VALUE_TYPE_BOOLEAN ||
        vt == AM_COMPILER_VALUE_TYPE_NULL ||
        vt == AM_COMPILER_VALUE_TYPE_UNDEFINED ||
        vt == AM_COMPILER_VALUE_TYPE_WCHAR) {
        return emit_instruction(ctx, AM_VM_OP_push, v);
    }

    return -1;
}


// 编译一般的值：根据类型生成push/load/loadclosure等指令
static int32_t compile_value(am_compiler_ctx_t *ctx, am_value_t v) {
    if (!ctx) return -1;

    int32_t vt = compiler_value_type(v);
    if (vt == AM_COMPILER_VALUE_TYPE_HANDLE) {
        am_handle_t h = am_value_to_handle(v);
        int32_t kind = compiler_node_kind(ctx, h);
        switch (kind) {
            case AM_COMPILER_NODE_KIND_LAMBDA:
                return emit_instruction(ctx, AM_VM_OP_loadclosure,
                                       am_compiler_make_label(ctx, v));
            case AM_COMPILER_NODE_KIND_QUOTE:
            case AM_COMPILER_NODE_KIND_STRING:
                return emit_instruction(ctx, AM_VM_OP_push, v);
            case AM_COMPILER_NODE_KIND_QUASIQUOTE:
                return compile_quasiquote(ctx, h);
            case AM_COMPILER_NODE_KIND_APPLICATION:
            case AM_COMPILER_NODE_KIND_UNQUOTE:
                return compile_application(ctx, h);
            default:
                return -1;
        }
    }

    if (vt == AM_COMPILER_VALUE_TYPE_SYMBOL) {
        int is_break;
        if (compiler_is_break_continue(v, &is_break) == 0) {
            am_value_t cond_tag, end_tag;
            if (while_tag_stack_top(ctx, &cond_tag, &end_tag) != 0) return -1;
            return emit_instruction(ctx, AM_VM_OP_goto, is_break ? end_tag : cond_tag);
        }
        return emit_instruction(ctx, AM_VM_OP_push, v);
    }

    if (vt == AM_COMPILER_VALUE_TYPE_VARID) {
        if (compiler_is_native_ref(ctx, v) == 0) {
            return emit_instruction(ctx, AM_VM_OP_push, v);
        }
        am_varid_t varid = am_value_to_varid(v);
        if (compiler_primitive_opcode_for_varid(ctx, varid) >= 0) {
            return emit_instruction(ctx, AM_VM_OP_push, v);
        }
        return emit_instruction(ctx, AM_VM_OP_load, v);
    }

    if (vt == AM_COMPILER_VALUE_TYPE_NUMBER ||
        vt == AM_COMPILER_VALUE_TYPE_BOOLEAN ||
        vt == AM_COMPILER_VALUE_TYPE_NULL ||
        vt == AM_COMPILER_VALUE_TYPE_UNDEFINED ||
        vt == AM_COMPILER_VALUE_TYPE_WCHAR) {
        return emit_instruction(ctx, AM_VM_OP_push, v);
    }

    return -1;
}


// ===============================================================================
// Application 编译
// ===============================================================================

static int32_t compile_complex_application(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    if (node->length == 0) return 0;

    size_t n = node->length;
    size_t uid = ctx->unique_id_counter;
    ctx->unique_id_counter += 2;

    am_value_t apply_begin_idx  = am_make_value_of_uint(uid);
    am_value_t temp_lambda_idx  = am_make_value_of_uint(uid + 1);

    am_value_t apply_begin_label  = am_compiler_make_label(ctx, apply_begin_idx);
    am_value_t temp_lambda_label  = am_compiler_make_label(ctx, temp_lambda_idx);

    // goto apply_begin
    if (emit_instruction(ctx, AM_VM_OP_goto, apply_begin_label) != 0) return -1;

    // 临时lambda标签
    if (am_compiler_locate_label(ctx, temp_lambda_idx, ctx->icount) != 0) return -1;

    // 按逆序存储形式参数
    for (size_t i = n; i-- > 0;) {
        am_varid_t p = am_compiler_make_temp_varid(ctx, L"TEMP_LAMBDA_PARAM", temp_lambda_label, i);
        if (p == SIZE_MAX) return -1;
        if (emit_instruction(ctx, AM_VM_OP_store, am_make_value_of_varid(p)) != 0) return -1;
    }

    // 加载参数1..n-1
    for (size_t i = 1; i < n; i++) {
        am_varid_t p = am_compiler_make_temp_varid(ctx, L"TEMP_LAMBDA_PARAM", temp_lambda_label, i);
        if (p == SIZE_MAX) return -1;
        if (emit_instruction(ctx, AM_VM_OP_load, am_make_value_of_varid(p)) != 0) return -1;
    }

    // 尾调用参数0（被调用函数）
    am_varid_t p0 = am_compiler_make_temp_varid(ctx, L"TEMP_LAMBDA_PARAM", temp_lambda_label, 0);
    if (p0 == SIZE_MAX) return -1;
    if (emit_instruction(ctx, AM_VM_OP_tailcall, am_make_value_of_varid(p0)) != 0) return -1;

    // return
    if (emit_instruction(ctx, AM_VM_OP_return, AM_VALUE_UNDEFINED) != 0) return -1;

    // apply_begin标签
    if (am_compiler_locate_label(ctx, apply_begin_idx, ctx->icount) != 0) return -1;

    // 编译实参
    for (size_t i = 0; i < n; i++) {
        if (compile_value(ctx, am_list_get(ctx->ast->alloc, node, i)) != 0) return -1;
    }

    // 调用临时lambda
    return emit_instruction(ctx, AM_VM_OP_call, temp_lambda_label);
}


static int32_t compile_application(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    if (node->length == 0) return 0;

    am_value_t first = am_list_get(ctx->ast->alloc, node, 0);

    // 特殊形式
    if (am_value_is_symbol(first)) {
        am_symbol_t sym = am_value_to_symbol(first);
        if (sym == am_value_to_symbol(AM_VALUE_KW_import))  return 0;
        if (sym == am_value_to_symbol(AM_VALUE_KW_native))  return 0;
        if (sym == am_value_to_symbol(AM_VALUE_KW_begin))   return compile_begin(ctx, handle);
        if (sym == am_value_to_symbol(AM_VALUE_KW_define))  return compile_define(ctx, handle);
        if (sym == am_value_to_symbol(AM_VALUE_KW_set))     return compile_set(ctx, handle);
        if (sym == am_value_to_symbol(AM_VALUE_KW_cond))    return compile_cond(ctx, handle);
        if (sym == am_value_to_symbol(AM_VALUE_KW_if))      return compile_if(ctx, handle);
        if (sym == am_value_to_symbol(AM_VALUE_KW_while))   return compile_while(ctx, handle);
        if (sym == am_value_to_symbol(AM_VALUE_KW_and))     return compile_and(ctx, handle);
        if (sym == am_value_to_symbol(AM_VALUE_KW_or))      return compile_or(ctx, handle);
    }

    // 首项是待求值的Application，需要进行η变换
    if (am_value_is_handle(first) &&
        compiler_node_kind(ctx, am_value_to_handle(first)) == AM_COMPILER_NODE_KIND_APPLICATION) {
        return compile_complex_application(ctx, handle);
    }

    // 首项是合法的可调用项，包括变量、Native、Primitive、Lambda
    if (am_value_is_handle(first) || am_value_is_varid(first) || am_value_is_symbol(first)) {
        // call/cc 与 fork 是全局内置变量形式的特殊形式
        if (am_value_is_varid(first)) {
            am_varid_t first_varid = am_value_to_varid(first);
            if (compiler_varid_name_equals(ctx, first_varid, L"call/cc") == 0) {
                return compile_callcc(ctx, handle);
            }
            if (compiler_varid_name_equals(ctx, first_varid, L"fork") == 0) {
                if (node->length < 2) return -1;
                return emit_instruction(ctx, AM_VM_OP_fork,
                                       am_list_get(ctx->ast->alloc, node, 1));
            }
        }

        // 处理参数列表
        for (size_t i = 1; i < node->length; i++) {
            if (compile_value(ctx, am_list_get(ctx->ast->alloc, node, i)) != 0) return -1;
        }

        // Primitive（关键字或全局内置变量）
        if (am_value_is_symbol(first)) {
            // 在C实现中，关键字符号作为特殊形式已在上方处理；剩余情况按不可调用处理
            return -1;
        }

        if (am_value_is_varid(first)) {
            int32_t opcode = compiler_primitive_opcode_for_varid(ctx, am_value_to_varid(first));
            if (opcode >= 0) {
                return emit_instruction(ctx, (uint32_t)opcode, AM_VALUE_UNDEFINED);
            }
        }

        // 尾调用判断
        int32_t is_tail = compiler_is_tailcall(ctx, handle);
        uint32_t call_opcode = (is_tail == 0) ? AM_VM_OP_tailcall : AM_VM_OP_call;

        if (am_value_is_handle(first) &&
            compiler_node_kind(ctx, am_value_to_handle(first)) == AM_COMPILER_NODE_KIND_LAMBDA) {
            return emit_instruction(ctx, call_opcode, am_compiler_make_label(ctx, first));
        }
        else if (am_value_is_varid(first)) {
            return emit_instruction(ctx, call_opcode, first);
        }

        return -1;
    }

    return -1;
}


// ===============================================================================
// Lambda 编译
// ===============================================================================

// TODO 处理pop问题
static int32_t compile_lambda(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);

    // 定位lambda开始标签
    if (am_compiler_locate_label(ctx, am_make_value_of_handle(handle), ctx->icount) != 0) return -1;

    // 按参数列表逆序，插入store指令
    am_uint_t n_param = compiler_lambda_param_count(node);
    for (am_int_t i = (am_int_t)n_param - 1; i >= 0; i--) {
        am_value_t param = am_list_get(ctx->ast->alloc, node, (size_t)(2 + i));
        if (emit_instruction(ctx, AM_VM_OP_store, param) != 0) return -1;
    }

    // 逐个编译函数体
    size_t n_body = 0;
    am_value_t *bodies = am_list_lambda_get_bodies(ctx->ast->alloc, node, &n_body);
    if (n_body > 0 && !bodies) return -1;
    for (size_t i = 0; i < n_body; i++) {
        if (compile_value(ctx, bodies[i]) != 0) {
            free(bodies);
            return -1;
        }
    }
    free(bodies);

    return emit_instruction(ctx, AM_VM_OP_return, AM_VALUE_UNDEFINED);
}


// ===============================================================================
// 特殊形式编译
// ===============================================================================

static int32_t compile_callcc(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    if (node->length < 2) return -1;

    am_value_t thunk = am_list_get(ctx->ast->alloc, node, 1);

    // 用于标识此call/cc的唯一标签
    am_value_t thunk_label = am_compiler_make_label(ctx, thunk);
    am_varid_t cont_varid = am_compiler_make_temp_varid(ctx, L"CC", thunk_label,
                                                         ctx->unique_id_counter++);
    if (cont_varid == SIZE_MAX) return -1;
    am_value_t cont_idx = am_make_value_of_varid(cont_varid);

    // capturecc cont_varid
    if (emit_instruction(ctx, AM_VM_OP_capturecc, cont_idx) != 0) return -1;
    // load cont_varid
    if (emit_instruction(ctx, AM_VM_OP_load, cont_idx) != 0) return -1;

    // 调用thunk
    if (am_value_is_handle(thunk) &&
        compiler_node_kind(ctx, am_value_to_handle(thunk)) == AM_COMPILER_NODE_KIND_LAMBDA) {
        if (emit_instruction(ctx, AM_VM_OP_call, am_compiler_make_label(ctx, thunk)) != 0) return -1;
    }
    else if (am_value_is_varid(thunk)) {
        if (emit_instruction(ctx, AM_VM_OP_call, thunk) != 0) return -1;
    }
    else {
        return -1;
    }

    // 续体返回点标签
    return am_compiler_locate_label(ctx, cont_idx, ctx->icount);
}


static int32_t compile_define(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    if (node->length < 3) return -1;

    am_value_t left = am_list_get(ctx->ast->alloc, node, 1);
    am_value_t right = am_list_get(ctx->ast->alloc, node, 2);

    if (!am_value_is_varid(left)) return -1;

    // 编译右值：lambda节点直接push其标签，其他按普通值编译
    if (am_value_is_handle(right) &&
        compiler_node_kind(ctx, am_value_to_handle(right)) == AM_COMPILER_NODE_KIND_LAMBDA) {
        if (emit_instruction(ctx, AM_VM_OP_push, am_compiler_make_label(ctx, right)) != 0) return -1;
    }
    else {
        if (compile_value(ctx, right) != 0) return -1;
    }

    return emit_instruction(ctx, AM_VM_OP_store, left);
}


static int32_t compile_set(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    if (node->length < 3) return -1;

    am_value_t left = am_list_get(ctx->ast->alloc, node, 1);
    am_value_t right = am_list_get(ctx->ast->alloc, node, 2);

    if (!am_value_is_varid(left)) return -1;

    // 编译右值：lambda节点生成闭包实例，其他按普通值编译
    if (am_value_is_handle(right) &&
        compiler_node_kind(ctx, am_value_to_handle(right)) == AM_COMPILER_NODE_KIND_LAMBDA) {
        if (emit_instruction(ctx, AM_VM_OP_loadclosure, am_compiler_make_label(ctx, right)) != 0) return -1;
    }
    else {
        if (compile_value(ctx, right) != 0) return -1;
    }

    return emit_instruction(ctx, AM_VM_OP_set, left);
}


// 编译begin节点：依次求值并保留最后一个表达式的结果
// TODO 处理pop问题
static int32_t compile_begin(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    if (node->length <= 1) return 0;

    for (size_t i = 1; i < node->length; i++) {
        if (compile_value(ctx, am_list_get(ctx->ast->alloc, node, i)) != 0) return -1;
        if (i < node->length - 1) {
            if (emit_instruction(ctx, AM_VM_OP_pop, AM_VALUE_UNDEFINED) != 0) return -1;
        }
    }
    return 0;
}


static int32_t compile_cond(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    size_t n = node->length;
    if (n < 2) return -1;

    // COND_END 标签：使用临时变量作为索引，确保同一 cond 的结束标签唯一
    am_varid_t end_lbl_varid = am_compiler_make_temp_varid(
        ctx, L"COND_END", am_make_value_of_handle(handle), 0);
    if (end_lbl_varid == SIZE_MAX) return -1;
    am_value_t end_lbl_idx = am_make_value_of_varid(end_lbl_varid);
    am_value_t end_lbl = am_compiler_make_label(ctx, end_lbl_idx);

    for (size_t i = 1; i < n; i++) {
        // 插入分支开始标签（第一个分支的标签实际上不会被引用，但为统一逻辑仍定位）
        am_varid_t branch_lbl_varid = am_compiler_make_temp_varid(
            ctx, L"COND_BRANCH", am_make_value_of_handle(handle), i);
        if (branch_lbl_varid == SIZE_MAX) return -1;
        am_value_t branch_lbl_idx = am_make_value_of_varid(branch_lbl_varid);
        am_value_t branch_lbl = am_compiler_make_label(ctx, branch_lbl_idx);
        (void)branch_lbl;
        if (am_compiler_locate_label(ctx, branch_lbl_idx, ctx->icount) != 0) return -1;

        am_value_t clause_handle = am_list_get(ctx->ast->alloc, node, i);
        if (!am_value_is_handle(clause_handle)) return -1;
        am_value_t clause_val = am_ast_get_node(ctx->ast, am_value_to_handle(clause_handle));
        if (!am_value_is_ptr(clause_val)) return -1;
        am_list_t *clause = (am_list_t *)am_value_to_ptr(clause_val);
        if (clause->length < 2) return -1;

        am_value_t predicate = am_list_get(ctx->ast->alloc, clause, 0);
        am_value_t branch = am_list_get(ctx->ast->alloc, clause, 1);

        int32_t is_else = am_value_is_symbol(predicate) &&
                          am_value_to_symbol(predicate) == am_value_to_symbol(AM_VALUE_KW_else);

        if (!is_else) {
            if (compile_predicate(ctx, predicate) != 0) return -1;
            if (i == n - 1) {
                if (emit_instruction(ctx, AM_VM_OP_iffalse, end_lbl) != 0) return -1;
            }
            else {
                am_varid_t next_branch_lbl_varid = am_compiler_make_temp_varid(
                    ctx, L"COND_BRANCH", am_make_value_of_handle(handle), i + 1);
                if (next_branch_lbl_varid == SIZE_MAX) return -1;
                am_value_t next_branch_lbl_idx = am_make_value_of_varid(next_branch_lbl_varid);
                am_value_t next_branch_lbl = am_compiler_make_label(ctx, next_branch_lbl_idx);
                if (emit_instruction(ctx, AM_VM_OP_iffalse, next_branch_lbl) != 0) return -1;
            }
        }

        if (compile_value(ctx, branch) != 0) return -1;

        if (is_else || i == n - 1) {
            return am_compiler_locate_label(ctx, end_lbl_idx, ctx->icount);
        }
        else {
            if (emit_instruction(ctx, AM_VM_OP_goto, end_lbl) != 0) return -1;
        }
    }

    return 0;
}


static int32_t compile_if(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    if (node->length < 3) return -1; // 至少需要predicate和true分支

    am_value_t predicate = am_list_get(ctx->ast->alloc, node, 1);
    am_value_t true_branch = am_list_get(ctx->ast->alloc, node, 2);

    size_t uid = ctx->unique_id_counter;
    ctx->unique_id_counter += 2;
    am_value_t true_label_idx = am_make_value_of_uint(uid);
    am_value_t true_label = am_compiler_make_label(ctx, true_label_idx);
    am_value_t end_label_idx = am_make_value_of_uint(uid + 1);
    am_value_t end_label = am_compiler_make_label(ctx, end_label_idx);

    if (compile_predicate(ctx, predicate) != 0) return -1;

    if (node->length > 3) {
        am_value_t false_branch = am_list_get(ctx->ast->alloc, node, 3);
        if (emit_instruction(ctx, AM_VM_OP_iftrue, true_label) != 0) return -1;
        if (compile_value(ctx, false_branch) != 0) return -1;
        if (emit_instruction(ctx, AM_VM_OP_goto, end_label) != 0) return -1;
        if (am_compiler_locate_label(ctx, true_label_idx, ctx->icount) != 0) return -1;
        if (compile_value(ctx, true_branch) != 0) return -1;
    }
    else {
        if (emit_instruction(ctx, AM_VM_OP_iffalse, end_label) != 0) return -1;
        if (compile_value(ctx, true_branch) != 0) return -1;
    }

    return am_compiler_locate_label(ctx, end_label_idx, ctx->icount);
}


static int32_t compile_while(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    if (node->length < 3) return -1;

    size_t uid = ctx->unique_id_counter;
    ctx->unique_id_counter += 2;
    am_value_t cond_label_idx = am_make_value_of_uint(uid);
    am_value_t cond_label = am_compiler_make_label(ctx, cond_label_idx);
    am_value_t end_label_idx = am_make_value_of_uint(uid + 1);
    am_value_t end_label = am_compiler_make_label(ctx, end_label_idx);

    if (while_tag_stack_push(ctx, cond_label, end_label) != 0) return -1;

    if (am_compiler_locate_label(ctx, cond_label_idx, ctx->icount) != 0) return -1;
    if (compile_predicate(ctx, am_list_get(ctx->ast->alloc, node, 1)) != 0) return -1;
    if (emit_instruction(ctx, AM_VM_OP_iffalse, end_label) != 0) return -1;
    for (size_t i = 2; i < node->length; i++) {
        if (compile_value(ctx, am_list_get(ctx->ast->alloc, node, i)) != 0) return -1;
    }
    if (emit_instruction(ctx, AM_VM_OP_goto, cond_label) != 0) return -1;
    if (am_compiler_locate_label(ctx, end_label_idx, ctx->icount) != 0) return -1;

    return while_tag_stack_pop(ctx);
}


static int32_t compile_and(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    size_t n = node->length;

    size_t uid = ctx->unique_id_counter;
    ctx->unique_id_counter += 2;
    am_value_t end_label_idx = am_make_value_of_uint(uid);
    am_value_t end_label = am_compiler_make_label(ctx, end_label_idx);
    am_value_t false_label_idx = am_make_value_of_uint(uid + 1);
    am_value_t false_label = am_compiler_make_label(ctx, false_label_idx);

    for (size_t i = 1; i < n; i++) {
        if (compile_value(ctx, am_list_get(ctx->ast->alloc, node, i)) != 0) return -1;
        if (emit_instruction(ctx, AM_VM_OP_iffalse, false_label) != 0) return -1;
    }

    if (emit_instruction(ctx, AM_VM_OP_push, AM_VALUE_TRUE) != 0) return -1;
    if (emit_instruction(ctx, AM_VM_OP_goto, end_label) != 0) return -1;
    if (am_compiler_locate_label(ctx, false_label_idx, ctx->icount) != 0) return -1;
    if (emit_instruction(ctx, AM_VM_OP_push, AM_VALUE_FALSE) != 0) return -1;
    return am_compiler_locate_label(ctx, end_label_idx, ctx->icount);
}


static int32_t compile_or(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);
    size_t n = node->length;

    size_t uid = ctx->unique_id_counter;
    ctx->unique_id_counter += 2;
    am_value_t end_label_idx = am_make_value_of_uint(uid);
    am_value_t end_label = am_compiler_make_label(ctx, end_label_idx);
    am_value_t true_label_idx = am_make_value_of_uint(uid + 1);
    am_value_t true_label = am_compiler_make_label(ctx, true_label_idx);

    for (size_t i = 1; i < n; i++) {
        if (compile_value(ctx, am_list_get(ctx->ast->alloc, node, i)) != 0) return -1;
        if (emit_instruction(ctx, AM_VM_OP_iftrue, true_label) != 0) return -1;
    }

    if (emit_instruction(ctx, AM_VM_OP_push, AM_VALUE_FALSE) != 0) return -1;
    if (emit_instruction(ctx, AM_VM_OP_goto, end_label) != 0) return -1;
    if (am_compiler_locate_label(ctx, true_label_idx, ctx->icount) != 0) return -1;
    if (emit_instruction(ctx, AM_VM_OP_push, AM_VALUE_TRUE) != 0) return -1;
    return am_compiler_locate_label(ctx, end_label_idx, ctx->icount);
}


static int32_t compile_quasiquote(am_compiler_ctx_t *ctx, am_handle_t handle) {
    am_value_t node_val = am_ast_get_node(ctx->ast, handle);
    if (!am_value_is_ptr(node_val)) return -1;
    am_list_t *node = (am_list_t *)am_value_to_ptr(node_val);

    for (size_t i = 0; i < node->length; i++) {
        am_value_t child = am_list_get(ctx->ast->alloc, node, i);
        int32_t vt = compiler_value_type(child);

        if (vt == AM_COMPILER_VALUE_TYPE_HANDLE) {
            int32_t kind = compiler_node_kind(ctx, am_value_to_handle(child));
            if (kind == AM_COMPILER_NODE_KIND_APPLICATION ||
                kind == AM_COMPILER_NODE_KIND_UNQUOTE) {
                if (compile_application(ctx, am_value_to_handle(child)) != 0) return -1;
            }
            else if (kind == AM_COMPILER_NODE_KIND_QUASIQUOTE) {
                if (compile_quasiquote(ctx, am_value_to_handle(child)) != 0) return -1;
            }
            else {
                if (emit_instruction(ctx, AM_VM_OP_push, child) != 0) return -1;
            }
        }
        else if (vt == AM_COMPILER_VALUE_TYPE_SYMBOL) {
            int is_break;
            if (compiler_is_break_continue(child, &is_break) == 0) return -1;
            if (emit_instruction(ctx, AM_VM_OP_push, child) != 0) return -1;
        }
        else if (vt == AM_COMPILER_VALUE_TYPE_VARID) {
            if (compiler_is_native_ref(ctx, child) == 0) {
                if (emit_instruction(ctx, AM_VM_OP_push, child) != 0) return -1;
            }
            else {
                if (emit_instruction(ctx, AM_VM_OP_load, child) != 0) return -1;
            }
        }
        else if (vt == AM_COMPILER_VALUE_TYPE_NUMBER ||
                 vt == AM_COMPILER_VALUE_TYPE_BOOLEAN ||
                 vt == AM_COMPILER_VALUE_TYPE_NULL ||
                 vt == AM_COMPILER_VALUE_TYPE_UNDEFINED ||
                 vt == AM_COMPILER_VALUE_TYPE_WCHAR) {
            if (emit_instruction(ctx, AM_VM_OP_push, child) != 0) return -1;
        }
        else {
            return -1;
        }
    }

    if (emit_instruction(ctx, AM_VM_OP_push, am_make_value_of_uint((am_uint_t)node->length)) != 0) return -1;
    return emit_instruction(ctx, AM_VM_OP_concat, AM_VALUE_UNDEFINED);
}


// ===============================================================================
// 编译器入口与标签解析
// ===============================================================================

int32_t am_compile_all(am_compiler_ctx_t *ctx) {
    if (!ctx || !ctx->ast) return -1;

    // 程序入口：调用顶级lambda
    am_handle_t top_lambda = ctx->ast->top_lambda_handle;
    if (top_lambda == AM_HANDLE_NULL) {
        top_lambda = am_ast_get_top_lambda_node_handle(ctx->ast);
    }
    if (top_lambda == AM_HANDLE_NULL) return -1;

    if (emit_instruction(ctx, AM_VM_OP_call,
                        am_compiler_make_label(ctx, am_make_value_of_handle(top_lambda))) != 0) {
        return -1;
    }
    if (emit_instruction(ctx, AM_VM_OP_halt, AM_VALUE_UNDEFINED) != 0) return -1;

    // 顺序编译所有lambda节点
    if (!ctx->ast->lambda_handles) return -1;
    for (size_t i = 0; i < ctx->ast->lambda_handles->length; i++) {
        am_value_t h = am_list_get(ctx->ast->alloc, ctx->ast->lambda_handles, i);
        if (!am_value_is_handle(h)) continue;
        if (compile_lambda(ctx, am_value_to_handle(h)) != 0) return -1;
    }

    return 0;
}


int32_t am_compiler_label_resolution(am_compiler_ctx_t *ctx) {
    if (!ctx || !ctx->ilcode) return -1;

    for (am_iaddr_t i = 0; i < ctx->icount; i++) {
        if (am_value_is_label(ctx->ilcode[i].operand)) {
            am_iaddr_t addr = am_compiler_parse_label_to_iaddr(ctx, ctx->ilcode[i].operand);
            if (addr == SIZE_MAX) return -1;
            ctx->ilcode[i].operand = am_make_value_of_iaddr(addr);
        }
    }
    return 0;
}


// ===============================================================================
// 上下文创建与销毁
// ===============================================================================

am_compiler_ctx_t *am_compiler_ctx_create(am_ast_t *ast) {
    if (!ast || !ast->alloc) return NULL;

    am_compiler_ctx_t *ctx = (am_compiler_ctx_t *)am_calloc(ast->alloc, sizeof(am_compiler_ctx_t));
    if (!ctx) return NULL;

    ctx->ast = ast;
    ctx->icount = 0;
    ctx->ilcode = NULL;
    ctx->label_counter = 0;
    ctx->unique_id_counter = 0;

    ctx->value_label_mapping = am_map_create(ast->alloc, 64);
    ctx->label_iaddr_mapping = am_map_create(ast->alloc, 64);
    ctx->while_tag_stack = am_list_create(ast->alloc, 16, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);

    if (!ctx->value_label_mapping || !ctx->label_iaddr_mapping || !ctx->while_tag_stack) {
        am_compiler_ctx_destroy(ctx);
        return NULL;
    }

    return ctx;
}


void am_compiler_ctx_destroy(am_compiler_ctx_t *ctx) {
    if (!ctx) return;

    am_allocator_t *alloc = ctx->ast ? ctx->ast->alloc : NULL;

    if (ctx->ilcode) {
        free(ctx->ilcode);
        ctx->ilcode = NULL;
    }

    if (alloc) {
        if (ctx->value_label_mapping) am_map_destroy(alloc, ctx->value_label_mapping);
        if (ctx->label_iaddr_mapping) am_map_destroy(alloc, ctx->label_iaddr_mapping);
        if (ctx->while_tag_stack) am_list_destroy(alloc, ctx->while_tag_stack);
        am_free(alloc, ctx);
    }
    else {
        free(ctx);
    }
}
