#ifndef __AM_LINKER_H__
#define __AM_LINKER_H__

#ifdef __cplusplus
extern "C" {
#endif




/*

模块链接过程中，对外部变量调用进行解引用的算法如下：

约定叫法：“主引模块”通过(import Mod "m.scm")和Mod.xxx引用“被引模块”"m.scm"及其顶级变量xxx。

举例说明如下：

;; 被引模块 Module A: /root/a.scm
(define f (lambda (x) (+ x 1)))

;; 主引模块 Module B: /root/b.scm
(import A "a.scm")
(display (A.f 666))

两个模块 parse + 对外引用分析 + alpha-renaming 后得到以下两个AST：

;; Module A 's AST
var_vocab = [ f , x , + , v.0.f , v.1.x , v.1.+ ]
var_arn_mapping = { 3:0 , 4:1 , 5:2  }
var_top = [ 3 ]
(define V3 (lambda (V4) (V5 V4 1)))

;; Module B 's AST
var_vocab = [ A , display , A.f , v.0.display ]
var_arn_mapping = { 3:1 }
variable_type = { 2:import_ref }
dependencies = { 0:"a.scm" }
var_top = [ ]
(import V0 "a.scm")
(V3 (V2 666))

链接时，链接器对主引模块进行扫描，扫描B模块所有非import非native节点中出现的varid，判断它的variable_type是否是AM_VAR_TYPE_IMPORT_REF。
如果否，则跳过。如果是，则从主引模块的var_vocab拿到变量的字符串，该例中是varid=2的"A.f"。
将其按“.”一分为二，分成prefix和suffix两部分。
其prefix是"A"，通过var_vocab反查到varid=0，据此从dependencies中找到被引模块"a.scm"，进而与环境目录一同得到模块ID=root.a，取到被引模块的AST。
其suffix是"f"，它应当是root.a模块的顶级变量，并且是Alpha-renaming前的变量。则通过am_ast_find_varid_of_external_ref，找到"f"在root.a中的varid=3。
这样，就确定了"A.f"对应的是root.a的AST中的V3。据此即可进行后面的AST融合。

*/




#ifdef __cplusplus
}
#endif

#endif
