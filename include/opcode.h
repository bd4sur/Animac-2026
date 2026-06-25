#ifndef __AM_OPCODE_H__
#define __AM_OPCODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "ast.h"

#define AM_VM_OP_nop         (0)
#define AM_VM_OP_store       (1)
#define AM_VM_OP_load        (2)
#define AM_VM_OP_loadclosure (3)
#define AM_VM_OP_push        (4)
#define AM_VM_OP_pop         (5)
#define AM_VM_OP_swap        (6)
#define AM_VM_OP_set         (7)
#define AM_VM_OP_call        (8)
#define AM_VM_OP_callnative  (9)
#define AM_VM_OP_tailcall    (10)
#define AM_VM_OP_return      (11)
#define AM_VM_OP_capturecc   (12)
#define AM_VM_OP_iftrue      (13)
#define AM_VM_OP_iffalse     (14)
#define AM_VM_OP_goto        (15)
#define AM_VM_OP_read        (16)
#define AM_VM_OP_write       (17)
#define AM_VM_OP_pause       (18)
#define AM_VM_OP_halt        (19)
#define AM_VM_OP_fork        (20)
#define AM_VM_OP_display     (21)
#define AM_VM_OP_newline     (22)
#define AM_VM_OP_add         (23)
#define AM_VM_OP_sub         (24)
#define AM_VM_OP_mul         (25)
#define AM_VM_OP_div         (26)
#define AM_VM_OP_mod         (27)
#define AM_VM_OP_eq          (28)
#define AM_VM_OP_eqv         (29)
#define AM_VM_OP_ge          (30)
#define AM_VM_OP_le          (31)
#define AM_VM_OP_gt          (32)
#define AM_VM_OP_lt          (33)
#define AM_VM_OP_not         (34)
#define AM_VM_OP_and         (35)
#define AM_VM_OP_or          (36)
#define AM_VM_OP_typeof      (37)
#define AM_VM_OP_car         (38)
#define AM_VM_OP_cdr         (39)
#define AM_VM_OP_cons        (40)
#define AM_VM_OP_get_item    (41)
#define AM_VM_OP_set_item    (42)
#define AM_VM_OP_list_push   (43)
#define AM_VM_OP_list_pop    (44)
#define AM_VM_OP_length      (45)
#define AM_VM_OP_concat      (46)
#define AM_VM_OP_duplicate   (47)



// 全局内置变量到 VM opcode 的映射表。
// 下标与 AM_GLOBAL_BUILTIN_VAR 一一对应；-1 表示该 builtin 没有对应 opcode。
extern const int32_t AM_BUILTIN_OPCODE_MAP[AM_GLOBAL_BUILTIN_VAR_NUM];




#ifdef __cplusplus
}
#endif

#endif
