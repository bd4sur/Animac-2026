#ifndef __AM_AST_H__
#define __AM_AST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "lexer.h"
#include "allocator.h"


// 顶级词法节点、顶级作用域和顶级闭包的parent字段，用于判断上溯结束
#define AM_TOP_NODE_HANDLE AM_VALUE_HANDLE_NULL



/*

AST数据结构设计：

- wchar_t *absolute_path; // 模块代码文件所在的文件系统绝对路径
- wchar_t *module_id; // 模块ID，从absolute_path转换而来，具体来说是把所有的斜杠替换成点号再倒序（类似Java的模块名规则）

- wchar_t *code; // 一切字符串的总源头，凡是源码里有的，都应该从code中取。
- am_token_t *tokens; // Lexer输出的token列表

- am_vocab_t *symbol_vocab; // 保存所有的symbol字符串（含换名前和换名后）集合，以其index为am_symbol_t

- am_vocab_t *var_vocab; // 保存所有的变量字符串（含换名前和换名后）集合，以其index为临时的varid（仅Parser阶段使用）
- // Map<varid, handle(token)> varid_token_mapping; // 记录varid与token的映射关系（对应TS的variableMapping）

- am_allocator_t *alloc; // 编译阶段AST专用的内存分配器
- Heap<handle, value> nodes; // AST临时堆，保存编译阶段所有数据对象（包括SList也就是AST节点、词法作用域、var/sym表等）的临时堆，它们之间都是通过handle互相引用，建立起树结构
- Map<handle, handle(token)> node_token_mapping; // 记录AST节点把柄与token的映射关系（对应TS的nodeIndexes）

- Map<handle(lambda), handle(scope)> scopes; // 词法作用域

- Map<varid, varid> variable_mapping; // 记录变量换名前后新旧varid的对应关系，用于报错。key是新的varid，value是旧的varid。（对应TS的variableMapping）

- List<handle> lambda_handles; // 记录所有的lambda节点的把柄（对应TS的lambdaHandles）
- List<handle> tailcall_handles; // 记录所有的尾调用节点的把柄（对应TS的tailcall）
- List<varid> topvar; // 顶级变量varid（即顶层作用域define的变量）（对应TS的topVariables）
- Map<varid, handle> dependencies; // 依赖模块记录，根据(import mod_alias "path/to/mod.scm")记录（对应TS的dependencies）
- Map<varid, handle> natives; // 本地库记录，根据(native Math)记录，其中handle可暂时设置为AM_VALUE_HANDLE_NULL备用

*/


typedef struct am_ast_t {

} am_ast_t;


// 功能描述：遍历tokens，使用其中的KEYWORD和SYMBOL构建ast->symbol_vocab，同时等于是注册了am_symbol_t，并将am_symbol_t记录在token中
// 实现说明：返回symbol总数。注意将object.h中定义的24个Keyword置于词典的前24个条目。
size_t am_build_symbol_vocabulary(am_ast_t *ast);


// 功能描述：遍历tokens，使用其中的VARIABLE构建ast->var_vocab，同时等于是注册了am_varid_t，并将varid记录在token中。
// 实现说明：返回varid总数。
size_t am_build_variable_vocabulary(am_ast_t *ast);



// 功能描述：判断某个变量是否是Native调用，是返回1，否返回0（对应TS的AST.IsNativeCall）
// 实现说明：(varid)(t.id)--[ast->var_vocab]-->var_str-->提取点号分隔的第1部分--[ast->var_vocab]-->native_varid--[ast->natives]-->是否存在
int32_t am_ast_is_native_call(am_ast_t *ast, am_token_t *t);


// 功能描述：根据把柄，从AST->nodes堆中获取相应的am_value_t（由调用者解包并使用）（对应TS的AST.GetNode）
// 设计说明：解释器的值-对象映射机制比较复杂。就本函数来说，根据handle从nodes堆中获得了am_value_t，这是一个打包后的值（因为只有打包后才能装进map、list等容器）。调用者通过该函数获得了am_value_t之后，应当自行判断其类型并解包。例如，如果调用者期望通过handle从nodes堆中获得一个am_object_t的指针，则通过本函数获得am_value_t后，将其作为am_object_t*也就是AM_VALUE_TYPE_PTR进行解包（使用am_value_to_ptr），这样就可以通过解包得到的ptr直接在C语言层面访问allocator管理的内存中（也就是被AST->nodes堆所封装起来的内存）的object对象。
am_value_t am_ast_get_node(am_ast_t *ast, am_handle_t handle);


// 功能描述：创建lambda对象，返回其在AST->nodes堆中的把柄（对应TS的AST.MakeLambdaNode）
// 实现说明：先从heap中申请一个把柄，再创建一个类型为AM_LIST_TYPE_LAMBDA的am_obj_list_t对象，以32为初始容量，再将对象指针打包成am_value_t与已分配把柄绑定在一起。同时，在ast->lambda_handles中登记这个把柄。最后返回把柄。如有异常情况，返回空把柄AM_VALUE_HANDLE_NULL，以示失败。
am_handle_t am_ast_make_lambda_node(am_ast_t *ast, am_handle_t parent);


// 功能描述：创建SList对象，返回其在AST->nodes堆中的把柄（对应TS的AST.MakeApplicationNode）
// 实现说明：先从heap中申请一个把柄，再创建一个类型为type=AM_LIST_TYPE_APPLICATION/AM_LIST_TYPE_QUOTE/AM_LIST_TYPE_QUASIQUOTE/AM_LIST_TYPE_UNQUOTE的am_obj_list_t对象，以32为初始容量，再将对象指针打包成am_value_t与已分配把柄绑定在一起。最后返回把柄。如有异常情况，返回空把柄AM_VALUE_HANDLE_NULL，以示失败。
am_handle_t am_ast_make_slist_node(am_ast_t *ast, am_handle_t parent, int32_t type);


// 功能描述：创建WString对象，返回其在AST->nodes堆中的把柄（对应TS的AST.MakeStringNode）
// 实现说明：先从AST->nodes中申请一个把柄，再根据AM_TOKEN_TYPE_STRING类型的am_token_t t 所表示的字符串（注意：可以根据其指示的index和length从ast->code中获取），创建一个am_obj_wstring_t对象，再将对象指针打包成am_value_t与已分配把柄绑定在一起。最后返回把柄。如有异常情况，返回空把柄AM_VALUE_HANDLE_NULL，以示失败。
am_handle_t am_ast_make_wstring_node(am_ast_t *ast, am_token_t *str_token);


// 功能描述：查找AST->nodes堆中最顶级am_obj_list_t对象的handle，也就是parent字段为AM_VALUE_HANDLE_NULL的am_obj_list_t对象。（对应TS的AST.TopApplicationNodeHandle）
// 设计说明：根据编译器的约定，合法Scheme代码的顶层结构应当是一个thunk的调用，即((lambda () ...))，这个函数就是用来获取这个顶层APPLICATION的。
// 实现说明：如有异常情况，返回空把柄AM_VALUE_HANDLE_NULL，以示失败。
am_handle_t am_ast_get_top_node_handle(am_ast_t *ast);


// 功能描述：查找AST->nodes堆中顶级am_obj_list_t（Lambda）对象的handle，也就是最顶级application list对象的第一个child。（对应TS的AST.TopLambdaNodeHandle）
// 设计说明：根据编译器的约定，合法Scheme代码的顶层结构应当是一个thunk的调用，即((lambda () ...))，这个函数就是用来获取这个顶层APPLICATION的第一个child也就是顶层lambda（thunk）的。
// 实现说明：如有异常情况，返回空把柄AM_VALUE_HANDLE_NULL，以示失败。
am_handle_t am_ast_get_top_lambda_node_handle(am_ast_t *ast);


// 功能描述：获取位于全局作用域的node列表（也就是函数体列表）。（对应TS的AST.GetGlobalNodes）
// 设计说明：取am_ast_get_top_lambda_node_handle也就是顶层lambda（thunk）的bodies，返回一个am_value_t的数组，由调用者负责解包、解释、释放。
// 实现说明：如有异常情况，返回NULL，以示失败。
am_value_t *am_ast_get_global_nodes(am_ast_t *ast);



// 功能描述：设置全局作用域（顶层lambda）的node列表（也就是函数体列表）。（对应TS的AST.SetGlobalNodes）
// 设计说明：用bodies整体替换am_ast_get_top_lambda_node_handle也就是顶层lambda（thunk）的bodies。
// 实现说明：通过am_list_lambda_set_bodies实现，这个过程可能涉及lambda对象指针的变化，如有变化，则更新AST->nodes中对应handle的值（打包成am_value_t的）。如有扩容失败等异常情况，返回0。执行成功则返回1。
int32_t am_ast_set_global_nodes(am_ast_t *ast, am_value_t *bodies);




// 功能描述：从某个节点开始，向上上溯查找某个varid归属的lambda节点把柄，也就是该varid作为哪个lambda节点的parameter（对应TS的Analyser.ts中的searchVarLambdaHandle）
// 设计说明：该函数用于“变量换名”过程，旨在寻找其最近上级lambda节点的把柄，进而确定其所在的词法作用域。该函数依赖于AST第一趟扫描“作用域分析”的结果。
// 实现说明：该函数的输入是变量换名前的varid，以及上溯查找起点节点的handle。如有异常情况，返回空把柄AM_VALUE_HANDLE_NULL，以示失败。
am_handle_t am_ast_find_var_lambda_handle(am_ast_t *ast, am_varid_t varid, am_handle_t from_node_handle);


// 功能描述：从某个节点开始，向上上溯查找最近的lambda节点的把柄（对应TS的Analyser.ts中的nearestLambdaHandle）
// 设计说明：该函数用于确定某个节点最近上级lambda节点的把柄，进而确定其所在的词法作用域。
// 实现说明：该函数的输入是上溯查找起点节点的handle。如有异常情况，返回空把柄AM_VALUE_HANDLE_NULL，以示失败。
am_handle_t am_ast_find_nearest_lambda_handle(am_ast_t *ast, am_handle_t from_node_handle);


// 功能表述：生成模块（AST）内唯一的变量名（对应TS的Analyser.ts中的MakeUniqueVariable）
// 设计说明：该函数用于“变量换名”阶段，用于生成携带作用域信息的、全局唯一的变量名，并将其新增注册到ast->var_vocab中。
// 实现说明：基于varid和所在lambda节点的handle，生成一个新的变量名字符串。规则是："V.module_id.lambda_handle.var_string"，并将其新增注册到ast->var_vocab，返回值是新变量名的ast->var_vocab的index。如有异常情况，返回SIZE_MAX，以示失败。
am_varid_t am_ast_make_unique_variable(am_ast_t *ast, am_varid_t varid, am_handle_t lambda_handle);








#ifdef __cplusplus
}
#endif

#endif
