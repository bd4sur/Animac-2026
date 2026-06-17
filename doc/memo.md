TODO ast.am_wstring_create 挪到wstring.c中实现；并补全wstring实现

TODO parser创建wstring对象时要先去掉两端的双引号

TODO heap.am_heap_set 不允许直接创建把柄。把柄必须遵循先申请后使用的原则。因此AST.am_ast_merge等用户必须修改。

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

NOTE symbol是以其字面为ID的，相同拼写的symbol，无论在哪个上下文中都是同一个符号。因此AST合并时，字符串相同的symbol，就是同一个symbol。这与variable截然不同。（当然，如果parse后把所有的variable都扩展成全限定的，那就几乎等同于符号了）


NOTE 关于解释器的工作目录、模块全局ID
- 任何解释器实例都必须指定一个基准工作目录(base_dir)。
- 若工作在REPL模式下，则以终端cwd为base_dir，给模块一个临时文件名
- 若运行代码文件，则以该文件所在目录为base_dir，即该文件绝对路径的目录部分（约定不带斜杠）。
- 所有的import文件路径，要么是绝对路径，要么是相对于base_dir的相对路径。
- 链接器搜索import模块的算法：
  - 判断import文件路径是绝对路径还是相对路径。如果是绝对路径，直接读取。
  - 如果是相对路径，则将base_dir与相对路径拼接成绝对路径再读取。
- 模块ID的构造规则：
  - 将模块绝对路径中的斜杠替换为点、空白字符替换成下划线、冒号去掉。
  - 去掉第一个点；若文件名有.scm后缀则去掉
  - 例如："/home/a/b.scm" -> home.a.b
- 链接器维护从模块ID到AST之间的映射关系：通过两个共享index的数组（字典）实现。
  - 拓扑排序时即以模块ID在这个vocab中的index为排序对象，AST数组也以这个index为index。

NOTE 关于本地native函数
- 所有的内置函数+运算符都是本地函数，作为顶级符号绑定在顶级作用域中，全局可见。
- 谨慎设计VMAPI


TODO 返回int的函数，全盘采用“正面非负、负面负数”的语义约定。并将is_xxx改名为check_xxx。以下是初步盘点：
成功（肯定）0，失败（否定）-1的：
am_closure_destroy
am_heap_set
parse_string
am_lexer
am_map_rehash
am_map_clear
am_map_destroy
am_map_set_stable
is_identifier_token -> is_term_start_token
成功（肯定）1，失败（否定）0：
am_closure_is_dirty_var
am_closure_has_bound_var
am_closure_has_free_var
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

1) AST增加Map成员variable_type<varid, uint>，用于记录变量的属性（是否是点号分隔变量），其中value是枚举值：

- AM_VAR_TYPE_DEFAULT (0) // 普通变量（没有用点号分隔的普通变量）
- AM_VAR_TYPE_IMPORT_REF (1) // 点号分隔的引用了外部import模块的变量，例如Mod.foo
- AM_VAR_TYPE_NATIVE_REF (2) // 点号分隔的对native函数的调用，例如"Math.exp"
- AM_VAR_TYPE_IMPORT_ALIAS (3) // 引用外部模块的别名，也就是(import Mod "mod.scm")中的Mod
- AM_VAR_TYPE_NATIVE_ID (4) // native模块名，也就是(native Math)中的Math


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



NOTE 模块链接算法

提前分配相关元数据（模块数上限设定为1024）
size_t module_counter(mod_index)
// am_vocab_t module_id -> mod_index
am_vocab_t module_abs_path -> mod_index
am_ast_t *ALLAST; // mod_index->ast
size_t DAG[][2]; // 邻接关系列表 importer_index -> importee_index
size_t sorted_ast_index[];

1) 从起始代码开始，解析为AST，读取所有导入文件，并逐个解析为AST，递归读取并解析，过程中完成：1收集AST；2构建DAG。
2) 对DAG做拓扑排序。
3) 从起始模块开始，按照拓扑排序顺序，逐个吃掉importee。每吃完一个importee，都做一次extref解引用（消歧），不能解引用的就暂时保留，待吃到后面的再处理。
4) extref解引用算法细节：
- 对extref变量，分割prefix和suffix
- 在ALLAST[importer_index]中：prefix(alias)->mod_path->mod_abs_path->importee_index
- 在ALLAST[importee_index]中：suffix->varid->在var_top中查找，找不到则报错，找到则通过var_ARN_mapping找到new_varid。


NOTE AST融合算法说明

merge(ast *importer, ast *importee, order); // 其中importer是大鱼（主引），importee是小鱼（被引），importee被importer吃掉，保留importer

第1步：修改importee。
- 将importee的symbol_vocab合并到importer的symbol_vocab，拿到新的am_symbol_t，建立old_id->new_id映射。
- 将importee的var_vocab合并到importer的var_vocab。OLD不重复添加，NEW因为有模块名，所以一定不重复



-------------------------------

NOTE 逻辑长度称length，物理长度称size，容器最大容量称capacity

NOTE parameter形式参数称“引数”，argument实际参数称“参数”

NOTE Alpha-renaming过程，也就是通过换名来消除嵌套词法作用域中同名变量的混淆的过程，简称为ARN。

NOTE 所有从heap中取出的变长容器类object，如果指针在操作后发生变化，必须将新指针的value写回map，以确保handle->value(ptr)->obj映射关系稳定！

NOTE 所有对外提供的函数，都加am_前缀。所有的宏，都加AM_前缀。

NOTE 关于函数返回值的语义约定。为了区分正面含义（肯定、正常、成功、找到）和负面含义（否定、异常、失败、没找到）两种语义，约定如下：

- int类返回值：以以负整数为负面含义，非负整数（含0）为正面含义。对于返回int的有谓词含义的函数，不要用is_xxx来命名，而应用check_xxx来命名，以避免与C语言对真假值的定义混淆。
- bool类返回值：此类函数几乎都应该用is_xxx的风格去命名，表示这是返回bool的谓词，以true为正面含义，以false为负面含义。如TPV的类型谓词：am_value_is_xxx，其返回值是bool类型，可直接通过if(is_xxx(xx))来使用。
- uint类返回值（如size_t）：一般含有index、长度之类的语义，0也是有意义的值，因此以SIZE_MAX为负面含义（如搜索未找到等），其余为正面含义。
- am_handle_t类返回值：以AM_HANDLE_NULL即UINTPTR_MAX为负面含义（如handle分配失败等），其余为正面含义。
- 指针类返回值：以NULL为负面含义（内存分配失败等），其余为正面含义。
- am_value_t类返回值：解包成各个基本类型后，遵循以上原则。






