TODO ast.am_wstring_create 挪到wstring.c中实现；并补全wstring实现

TODO parser创建wstring对象时要先去掉两端的双引号

TODO 在abcdefg用例中，似乎有把关键字塞进变量表的问题。

TODO 计算object对象size的宏或函数（用于内存管理）

TODO 计算object对象的hash（map也要计算？），用于相等性比较（内涵的、外延的，需精密考虑）

TODO 作为基础设施的字符串操作函数

TODO 未来将 alpha renaming 优化成 de Bruijn index

TODO 模块名称机制（全限定名之类的，以及模块名在Linker中的用法）

TODO 重要问题：module要不要作为语言内置对象存在？这样import就变成了一个内置过程，像是eval。
     - 想办法支持“具名作用域”机制（这不就是define吗），这样就不需要麻烦的merge过程了。
TODO 重要问题：链接器要不要放在IL层面？以支持module内置对象。

TODO AN前先绑定全局内置符号，内置符号全局有效不换名（与extref的处理策略一致）。

TODO 变量Alpha-renaming(ARN)规则：
- BUILTIN一律不ARN，保留原形。NOTE builtin的绑定关系不可修改，VM将其视为native_ref。
- IMPORT_REF、NATIVE_REF、IMPORT_ALIAS、NATIVE_ID都不ARN，保留原形。
- AN不会涉及new，一旦遇到new，则报错退出。
- 对于old类型的varid：ARN后的全限定名格式为"模块ID.定义所在Lambda的把柄.原名"
  - 例如：home.user.app.114514.x

TODO 将PathUtils单独实现出来。




NOTE 关于本地native函数
- 所有的内置函数+运算符都是本地函数，作为顶级符号绑定在顶级作用域中，全局可见。
- 谨慎设计VMAPI


TODO 返回int的函数，全盘采用“正面非负、负面负数”的语义约定。并将is_xxx改名为check_xxx。以下是初步盘点：
成功（肯定）0，失败（否定）-1的：
am_closure_destroy
am_closure_has_bound_var
am_closure_has_free_var
am_closure_is_dirty_var
am_heap_set
parse_string
am_lexer
am_map_rehash
am_map_clear
am_map_destroy
am_map_set_stable
is_identifier_token -> is_term_start_token
成功（肯定）1，失败（否定）0：
am_heap_destroy
is_number
is_keyword
parse_hash_literal
am_map_find_slot
am_map_contains -> am_heap_has_handle
am_map_delete -> am_heap_free_handle
am_list_destroy
am_list_set
append_child_to_top
add_parameter_to_top_lambda
add_body_to_top_lambda
am_scope_destroy
am_scope_has_var
am_vocab_destroy


TODO Linker中的跨模块引用换名问题：


2) 解析完dep和native后，变量换名之前，对AST再进行一次整体扫描：外部引用扫描。对于【非import和native节点中出现的】所有varid，执行以下动作：

- 首先从var_vocab中取到变量字符串，判断它是不是点号分隔的形式。
-- 如果不是，则为普通变量，在variable_type中，将其varid的属性值设为make_value(AM_VAR_TYPE_DEFAULT, uint)。
-- 如果是，则将其按“.”一分为二，分成prefix和suffix两部分。
--- 从var_vocab查询prefix对应的varid。
---- 如果没查到，则报错。
---- 如果查到了，则检查prefix对应的varid是否位于dependencies或natives中。
----- 如果都不存在，则报错（非外部依赖模块或native函数调用）。
----- 如果在dependencies中查到了，则在variable_type中，将该varid的属性值设为am_make_value_of_uint(AM_VAR_TYPE_IMPORT_REF)，将prefix对应的varid的属性值设为am_make_value_of_uint(AM_VAR_TYPE_IMPORT_ALIAS)。
----- 如果在natives中查到了，则在variable_type中，将该varid的属性值设为am_make_value_of_uint(AM_VAR_TYPE_NATIVE_REF)，将prefix对应的varid的属性值设为am_make_value_of_uint(AM_VAR_TYPE_NATIVE_ID)。
----- 如果两个都查到了，则报错退出（因为一个prefix不能既是import的又是native的）。

3) 在做alpha-renaming时，对于点号分隔的形式，不做替换。native和import节点中的变量也不做替换（已有）。

4) Alpha-renaming时，要拷贝variable_type属性。

4) 增加ast成员函数如下

// 功能描述：通过传入的变量名，找到该变量对应的顶级变量的varid。该函数用于链接过程中主引模块获得被引模块的ARN换名后varid。
// 设计说明：一般情况下，一个ARN前的varid，可能对应多个ARN后的varid。因此，单凭ARN前的varid，无法确定唯一的ARN后的varid。但是我们有以下规则：通过import机制引用其他模块的变量时，必须引用定义在顶层作用域的变量，也就是var_top列表中的变量，而顶层作用域的变量是唯一的。这样，在模块链接过程中，主引模块通过一个ARN前的变量字符串，可以唯一地确定一个ARN后的varid，这个varid必然是var_top。
// 实现说明：
// - 在ast->var_vocab中反查出varstr的varid
// - 遍历ast->var_top中的每个varid=v，通过ast->var_arn_mapping找到v对应的ARN换名前的varid，并与上一步反查出的varid比较。如果一致，则返回对应的v。
// - 这个v就是varstr在ARN换名后的varid，同时它也是模块的顶级变量。

am_varid_t am_ast_find_top_varid_of_external_ref(am_ast_t *ast, wchar_t *varstr);







