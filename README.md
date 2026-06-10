# Animac-2026

## 概念体系

对象语言概念体系

- 能指：keyword
- 能指/所指：imme = boolean | number | symbol
- 能指/所指：string
- 能指：variable
- 能指/所指：undefined
- 能指/所指：null
- 所指：list = lambda | application | quote | quasiquote | unquote
- 所指：closure
- 所指：continuation

元语言（中间语言+实现语言）概念体系

- 能指：mnemonic
- 能指：operand = imme | iaddr | varid | undefined | null
- 能指：value = imme | iaddr | handle | undefined | null
- 能指/所指：imme = boolean | number | symbol
- 能指/所指：iaddr 指令地址，改进后，编译器要消除人类可读的所谓的“标签”。
- 能指：varid 对象语言编译为中间语言后，指代对象语言variable的全局唯一的int32_t。
- 能指/所指：handle 对象在堆内的地址，类比为物理地址。既是中间语言的所指，又是实现语言的能指。
- 所指：obj = list_obj | string_obj | closure_obj | continuation_obj
- 所指：list_obj = lambda_obj | application_obj | quote_obj | quasiquote_obj | unquote_obj
- 所指：frame = [ handle(to closure_obj) , iaddr ]
- 所指：process = [ fstack , opstack , heap , PC , pid ... ]
- 所指：runtime

各记忆区内存储的是什么东西

- fstack = Array<frame>
- opstack = Array<value>
- closure_obj.bound = Map<varid, value>
- closure_obj.free = Map<varid, value>
- closure_obj.dirty_flag = Map<varid, int32_t>
- heap = Map<handle, obj> 注意此处的obj是实现语言中的结构体指针、物理地址一类的东西，概念上等于是obj

问题：

- nan是不是number？
- 内置运算符应当设置为symbol，而非keyword。不要滥用keyword。
- string为什么不归为imme？因为string实现上有其复杂性和特殊性。

## 实现语言的基础设施

- 字符串操作：包括正则表达式相关操作。
- Array（支持push、pop、unshift、随机访问、遍历、排序等）
- 以obj粒度进行读写的数组，支持GC。
- Map<int32_t, int32_t>，用于handle和物理地址转换。
- HashMap<string, int32_t>，用于保存元数据（各类映射表）

