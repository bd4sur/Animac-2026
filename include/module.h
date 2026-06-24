#ifndef __AM_MODULE_H__
#define __AM_MODULE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

#include "object.h"
#include "ast.h"
#include "compiler.h"


///////////////////////////////////////////
// 模块数据结构
// 说明：模块是编译器的输出产物，包含静态AST节点和中间语言指令序列。
//       进程由模块加载而来，模块本身不管理运行时状态。
///////////////////////////////////////////

typedef struct am_module_t {
    am_object_t base; // 基类头：am_module_t也视为对象语言的数据对象

    uint64_t header; // 保留：元数据头
    int32_t opstack_depth; // 编译期分析出来的opstack最大深度
    am_ast_t *ast;
    am_instruction_t *ilcode;
    am_iaddr_t ilcode_length; // ilcode数组长度（指令条数）
} am_module_t;


#ifdef __cplusplus
}
#endif

#endif
