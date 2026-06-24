TODO linker相对路径到绝对路径

TODO 遗留问题：进程初始化时为什么要新建一个顶级闭包？为了防止闭包链上溯时访问失败？

TODO 模块的转储（dump）和加载（load）

TODO capturecc指令的参数改成iaddr，其返回的把柄直接入栈

TODO TS：tailcall实现，复用CallAsync函数

TODO eq? eqv? equal? +typeof

TODO 长远：不许使用系统malloc

TODO 改进TPV格式：利用NaN

TODO 虚拟机中央switch性能优化（计算跳转、基于profiling的优化等）

TODO 表达式压栈和begin的pop问题

TODO parser创建wstring对象时要先去掉两端的双引号

TODO 在abcdefg用例中，似乎有把关键字塞进变量表的问题。

TODO 计算object对象size的宏或函数（用于内存管理）

TODO 计算object对象的hash（map也要计算？），用于相等性比较（内涵的、外延的，需精密考虑）

TODO 模块二进制文件的格式：魔数、版本号、module_id

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


NOTE 内存池在编译期和运行期之间要发生一次彻底重分配：编译期和运行期，都可以使用完整的内存池。编译完成后，将模块dump成一个完整的二进制文件，随后彻底清空内存池。

NOTE 关于本地native函数
- 所有的内置函数+运算符都是本地函数，作为顶级符号绑定在顶级作用域中，全局可见。
- 谨慎设计VMAPI


NOTE linker的输入参数保持AST不变：这样可以兼容文件、repl等多种场景。TODO REPL如何实现？


TODO 指令集设计变动

- capturecc iaddr 捕获当前Continuation，以iaddr为返回地址，并将其把柄入栈
- 算术逻辑运算：add、sub、mul、div、mod、eq、eqv、ge、le、gt、lt、not、and、or、typeof
- 列表运算：car、cdr、cons、get_item、set_item、append(->push)、(+pop)、length、concat、duplicate、、、
- fork机制要仔细考虑

TODO ILCode的序列化和反序列化

- 务必注意结构体填充和对齐问题
- 序列化时，opcode只占用一个字节（uint8_t）以减小代码长度。

TODO make_label
// 功能说明：根据handle、varid等值，构造一个全局唯一的临时label（增加值类型AM_VALUE_TYPE_LABEL）。相当于@<value>
// 只要lbl_value相等，在后面的标签替换阶段就会被替换为标签所在的iaddr
make_label(am_value_t lbl_value);
