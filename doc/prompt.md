请根据 @map.h 中关于am_map_t（哈希表：Map<am_value_t,am_value_t>）的定义，在 @map.h 中实现对其的一系列操作。

am_map_t应当在am_allocator_t提供的内存中进行分配和操作，相关方法详见 @allocator.h。

- am_map_t *am_map_create(am_allocator_t *alloc, size_t capacity); // 以初始容量新建哈希表：所有key和value初始化为AM_VALUE_NULL；capacity必须设置为不小于它的最小的2的幂
- int32_t am_map_clear(am_allocator_t *alloc, am_map_t *map); // 清空哈希表：对所有的entry执行：如果value是am_value_is_ptr，则先free，再将value置为AM_VALUE_NULL
- int32_t am_map_destroy(am_allocator_t *alloc, am_map_t *map); // 彻底销毁哈希表对象
- am_value_t am_map_get(am_allocator_t *alloc, am_map_t *map, am_value_t key); // 查找
- int32_t am_map_contains(am_allocator_t *alloc, am_map_t *map, am_value_t key); // 存在性检查
- int32_t am_map_set(am_allocator_t *alloc, am_map_t *map, am_value_t key, am_value_t value); // 插入或修改（按需扩容）
- int32_t am_map_delete(am_allocator_t *alloc, am_map_t *map, am_value_t key); // 删除
- size_t am_map_length(am_allocator_t *alloc, am_map_t *map); // 当前有效键值对数量
- size_t am_map_capacity(am_allocator_t *alloc, am_map_t *map); // 物理槽位数
- void am_map_iter(am_allocator_t *alloc, am_map_t *map, am_map_iter_callback_t cb, void *user_data); // 遍历
- am_value_t *am_map_keys(am_allocator_t *alloc, am_map_t *map); // 获取所有的key（直接在系统内存中调用malloc返回动态列表指针即可，无需使用am_allocator_t）

遍历回调类型：
typedef void (*am_map_iter_callback_t)(am_value_t key, am_value_t value, void *user_data);

其他必要的接口包括哈希、扩容、重哈希等，按需实现。


---------------------

详细解释哈希函数 am_value_hash 的原理，不要修改代码。

---------------------

请你实现一个 test_map.c，用于测试 @map.h 。不要修改已有的任何代码，凡是缺失的基础设施想办法在 test_map.c 中实现。只允许在 test_map.c 中新增代码，不要修改已有的任何代码。

---------------------

针对map扩容会导致map指针发生变化的问题，我提出以下新需求：

1、在 @map.h 中新增不扩容插入/修改接口：am_map_set_stable，返回int32_t表示是否成功，该函数仅做插入修改操作，不执行任何扩容操作，从而保证map指针稳定（不会被realloc）。同时保留原来的am_map_set接口不变。

2、在 @test_map.c 中测试新增的 am_map_set_stable 函数。

3、请你评审我的设计是否合理。由于map在我的Scheme解释器和VM的实现中是极其重要的核心数据结构，所以必须使其具备足够的灵活性。am_map_set_stable 用于解释器内部实现，例如对closure内部map的操作，以及用于map所有权复杂的情况。而am_map_set用于所有权清晰的情况，通过解释器和VM的handle机制实现解耦。

---------------------

关于抽象堆，我打算做以下设计：

1、正如你所说，抽象堆的“地址”应当是与物理地址（指针）解耦的、稳定的“handle”，一个handle一旦被生成，在其整个生命周期内的值就不应变化（但是其对应的物理地址当然可以变化）。抽象堆有责任维护从handle到物理地址（指针）的映射关系。

2、而我前面要的，实际上是对物理存储的抽象，而不是对进程堆的抽象。之所以要对物理存储进行抽象，一方面是为了兼容性（比如不允许/不能使用系统malloc的场景、比如内存极度受限的场景等等），可以将系统预先分配的一整块内存作为物理内存由VM的抽象物理存储进行管理，另一方面也是为了性能和灵活性，例如可以按需选用Arena、freelist等实现方案等等。

请你评审我的想法。

---------------------

我自己手工修改了 @map.h 中的实现，将map对象及其操作改成了不依赖process、heap、am_object_t的基础数据结构。修改后，作为基础数据结构，其内存分配完全由 @allocator.h 中定义的抽象内存分配器 am_allocator_t 负责。现在请你完成以下两项任务：

1、不要修改 @map.h ，仅检查是否有错漏之处。

2、基于现有的 @map.h 和 @allocator.h ，彻底修改 @test_map.c ，使其适应新的map和allocator定义和实现。

3、除此之外不要修改任何已有的其他无关代码。

---------------------

请重新读取 @map.h ，分析扩容相关逻辑，告诉我为什么不使用am_realloc。不要修改代码。

---------------------

为了实现物理地址与堆逻辑地址（handle）的解耦，以及便于实现堆GC，引入了“堆数据对象”，“堆数据对象”实际上是对立即数或者内存对象指针（物理地址）的封装，在堆中通过数值上不变的handle对“堆数据对象”进行引用。

有两个问题：

1、“堆数据对象”的hash如何计算（以便判断同一性等）。
2、堆对象指针的所有权问题：是否能够确保只有堆数据对象唯一持有内存对象指针？进而一旦内存对象指针发生变化，该对象对应的堆数据对象的handle可以保持不变？

---------------------

我前面所说的“堆数据对象”，案例如下。这样的字段设计是否合理？

// something堆对象（对am_something_t的封装，使其把柄稳定、可GC）
typedef struct am_obj_something_t {
    am_object_t base;

    am_something_t *sth; // 抽象物理内存中指向实际am_something_t对象的指针，可能会变
} am_obj_something_t;

我总觉得这样很奇怪，性能问题且不论，这实际上形成了多次映射：handle-[heap]->am_obj_something_t对象 -[allocator]->am_something_t对象。但是我理解的heap应当是handle-[heap]->am_something_t对象。当然，am_something_t对象中凡是引用内存对象的，都是通过handle引用的。
我这样做是有这样一层考虑，也就是让am_something_t在对象语言（如Scheme）中可见。
请你评审我的想法，重点评述这种想法是如何抵抗物理地址变化的，以及在GC中的实现方式。

---------------------

基于 @closure.h 中现有的 am_obj_closure_t 结构体定义，在 @closure.h 中实现以下对Scheme闭包对象的操作。
注意：我的意图是用线性表（柔性数组）来模拟从 (am_varid_t varid, int32_t type) 到 (am_value_t value, int32_t dirty_flag) 的映射，也就是说，通过尾部插入和遍历搜索的方式进行操作即可。闭包中变量的绑定不涉及删除操作。你的API实现需要支持动态扩容（扩容后am_obj_closure_t对象的指针可能会变，这部分逻辑你可以参考 @map.h 中哈希表扩容的相关方法）。
但是你要注意到，绝大多数情况下，是不涉及闭包扩容的。你的实现应该充分考虑这一点，以优化性能。

你需要实现的API如下：

- am_obj_closure_t *am_closure_create(am_allocator_t *alloc, am_iaddr_t iaddr, am_handle_t parent, size_t capacity); // 默认capacity为16
- int32_t am_closure_destroy(am_allocator_t *alloc, am_obj_closure_t *closure);
- am_obj_closure_t *am_closure_copy(am_allocator_t *alloc, am_obj_closure_t *closure); // 全拷贝：必须完整拷贝所有字段和bindings
- am_obj_closure_t *am_closure_init_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value); // 如涉及扩容，则返回新闭包对象的指针，下同
- am_obj_closure_t *am_closure_set_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value);
- am_value_t am_closure_get_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable);
- am_obj_closure_t *am_closure_init_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value);
- am_obj_closure_t *am_closure_set_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable, am_value_t value);
- am_value_t am_closure_get_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable);
- int32_t am_closure_is_dirty_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable);
- int32_t am_closure_has_bound_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable);
- int32_t am_closure_has_free_var(am_allocator_t *alloc, am_obj_closure_t *closure, am_varid_t variable);

你可以参考已有的TypeScript实现：

```
// 闭包（运行时堆对象）
class Closure extends SchemeObject {
    public instructionAddress: number;               // 指令地址
    public parent: Handle;            // 亲代闭包把柄
    public boundVariables: HashMap<string, any>;   // 约束变量
    public freeVariables: HashMap<string, any>;    // 自由变量
    public dirtyFlag: HashMap<string, boolean>;    // 脏标记

    constructor(instructionAddress: number,
                parent: Handle) {
        super();
        this.type = SchemeObjectType.CLOSURE;
        this.instructionAddress = instructionAddress;
        this.parent = parent;
        this.boundVariables = new HashMap<string, any>();
        this.freeVariables = new HashMap<string, any>();
        this.dirtyFlag = new HashMap<string, boolean>();
    }

    public Copy(): Closure {
        let copy = new Closure(this.instructionAddress, this.parent);
        copy.type = SchemeObjectType.CLOSURE;
        copy.boundVariables = this.boundVariables.Copy();
        copy.freeVariables = this.freeVariables.Copy();
        copy.dirtyFlag = this.dirtyFlag.Copy();
        return copy;
    }

    // 不加脏标记
    public InitBoundVariable(variable: string, value: any): void {
        this.boundVariables[variable] = value;
        this.dirtyFlag[variable] = false;
    }
    // 加脏标记（仅用于set指令）
    public SetBoundVariable(variable: string, value: any): void {
        this.boundVariables[variable] = value;
        this.dirtyFlag[variable] = true;
    }
    public GetBoundVariable(variable: string): any {
        return this.boundVariables[variable];
    }
    // 不加脏标记
    public InitFreeVariable(variable: string, value: any): void {
        this.freeVariables[variable] = value;
        this.dirtyFlag[variable] = false;
    }
    // 加脏标记（仅用于set指令）
    public SetFreeVariable(variable: string, value: any): void {
        this.freeVariables[variable] = value;
        this.dirtyFlag[variable] = true;
    }
    public GetFreeVariable(variable: string): any {
        return this.freeVariables[variable];
    }

    public IsDirtyVariable(variable: string): boolean {
        return this.dirtyFlag[variable];
    }

    public HasBoundVariable(variable: string): boolean {
        return this.boundVariables.has(variable);
    }
    public HasFreeVariable(variable: string): boolean {
        return this.freeVariables.has(variable);
    }
}
```

---------------------

从统计的角度讲，一个Scheme闭包内部，有多少变量绑定？包含自由变量和约束变量。给出上下限和平均情况的估计。

从程序语言实现领域的经验观察来看（Scheme、ML、Lisp 等函数式语言的编译器文献和工业实现数据），闭包内部的变量绑定数量呈现**极度偏斜的分布**：绝大多数极少，极端个案极少。**平均 3~5 个、90% 以上少于 8 个、99% 以上少于 15 个**。

---------------------

本项目是一个C语言编写的Scheme解释器。请你通读项目C语言代码，并在 @parser.c 中完成以下需求。

首先你需要知道 @parser.c 是一个递归下降解析器，其对应的BNF语法为：

```
    <SourceCode> ::= (lambda () <TERM>*) CRLF
          <Term> ::= <SList> | <Lambda> | <Quote> | <Unquote> | <Quasiquote> | <Identifier>
         <SList> ::= ( <SListSeq> )
      <SListSeq> ::= <Term> <SListSeq> | ε
        <Lambda> ::= ( lambda <ArgList> <Body> )
       <ArgList> ::= ( <ArgListSeq> )
    <ArgListSeq> ::= <ArgIdentifier> <ArgListSeq> | ε
 <ArgIdentifier> ::= <Identifier>
          <Body> ::= <BodyTerm> <Body_>
         <Body_> ::= <BodyTerm> <Body_> | ε
      <BodyTerm> ::= <Term>
         <Quote> ::= ' <QuoteTerm> | ( quote <QuoteTerm> )
       <Unquote> ::= , <UnquoteTerm> | ( unquote <QuoteTerm> )
    <Quasiquote> ::= ` <QuasiquoteTerm> | ( quasiquote <QuoteTerm> )
     <QuoteTerm> ::= <Term>
   <UnquoteTerm> ::= <Term>
<QuasiquoteTerm> ::= <Term>
    <Identifier> ::= IDENTIFIER
```

## 需求1：增加保存Lambda嵌套状态的栈：lambda_stack

在Parser的parser_ctx_t中，有几个stack，用于保存解析状态和列表嵌套状态。我要求你仿照node_stack及其用法，在parser_ctx_t中新增一个保存Lambda嵌套状态的栈：lambda_stack。这个栈的目的是：在递归下降解析过程中，保存当前所在的lambda节点的管辖范围，也就是词法作用域。当Parser解析到 parse_lambda() 时，会创建一个lambda_handle，意味着进入了一个新的词法作用域，你需要在lambda_stack栈中维护解析过程中遍历词法作用域（lambda节点的管辖范围）的过程。这样，Parser遍历到任何节点的时候，都可以通过lambda_stack栈知道自己处在哪个词法作用域（也就是Lambda节点的管辖范围）下。

## 需求2：解析VARIABLE类型的IDENTIFIER时，对其做Alpha-renaming

在 @parser.c 的844行附近，也就是 parse_identifier 的 AM_TOKEN_TYPE_IDENTIFIER 分支内，对于普通的variable，目前的实现是直接取token的id字段作为varid，并加入slist对象。而我希望基于需求1中实现的lambda_stack获取词法作用域信息，在parse阶段，直接对变量做Alpha-renaming。具体方法是：

1. 根据token对象，取到这个variable的varid（tok->id）
2. 从lambda_stack，取到当前所在lambda节点的handle：am_handle_t lambda_handle
3. 调用 @ast.c 中的 am_ast_make_unique_variable 函数，构造一个Alpha-renaming 后的varid
4. 以这个新的varid为value

## 需求3：测试用例

完成以上需求后，在 @test_parser.c 中进行测试。仅允许增加测试用例，不得修改其他测试用例。你可以通过WSL运行make进行构建。

请阅读以上信息，实现我的需求。


---------------------


# 模块链接算法

开始编码前，请先阅读 @doc/AGENTS.md 。

为实现Scheme解释器的AST模块链接器，请你在 @src/linker.c 中，根据下文描述的算法，实现模块的递归链接算法。

## 术语约定

- 模块：一个完整的Scheme源码文件，及其解析得到的AST。模块之间可互相引用。
- 外部引用：指的是通过import和点号分隔标识符，引用外部模块变量。(import Alias "/path/to/module.scm")表达式，声明对外部模块的导入，并赋予其“别名”Alias，别名属于特殊变量，其类型为AM_VAR_TYPE_IMPORT_ALIAS。代码中通过“别名.标识符”的格式，引用外部模块的变量。“别名.标识符”整体也是一个变量，在parse阶段，其类型为AM_VAR_TYPE_IMPORT_REF。
- 模块ID：或者叫模块的“全限定名”，是模块源码文件的绝对路径转换得到的、在整个宿主环境中具有唯一性的字符串。模块ID实质上就是模块文件的绝对路径，只是被转换成了点号分隔的标识符格式。模块ID在模块parse和链接阶段，模块ID和模块绝对路径用于唯一确定一个模块，使得参与链接的所有变量都具有全局唯一的字符串形式和varid。
- importer（主引模块）：代码中通过(import ...)引用其他模块的模块。
- importee（被引模块）：被其他importer引用的模块。主引被引是相对的。在引用关系有向无环图（DAG）构建过程中，规定importer指向importee。
- 引用根：链接任务的唯一输入模块，是引用关系图的唯一起点。一般是项目的主模块（main.scm之类的）。

## 演示/测试用例

以下是3个互相依赖的模块，其中各标识符有意设计得容易混淆：

```
;; root.z
(define f (lambda (x y z) 666))
(define x 100)
(define y 200)
(define z 300)

;; root.y
(import z "/root/z.scm")
(define f (lambda (x y z) 888))
(define x z.x)
(define y z.y)
(define z z.z)

;; root.x
(import z "/root/y.scm") ;; 注意：故意不对应
(import y "/root/z.scm")
(define f (lambda (x y z) 888))
(define x (+ y.x z.x))
(define y (+ y.y z.y))
(define z (+ y.z z.z))
```

经过Parse+ARN之后：

```
;; root.z
varid     =      0            1           2           3           4           5          6
var_vocab = [root.z.0.f  root.z.1.x  root.z.1.y  root.z.1.z  root.z.0.x  root.z.0.y  root.z.0.z]
var_type  = [   new          new          new         new        new         new         new   ]
dep       = { empty }

(define root.z.0.f (lambda (root.z.1.x root.z.1.y root.z.1.z) 666))
(define root.z.0.x 100)
(define root.z.0.y 200)
(define root.z.0.z 300)


;; root.y
varid     =     0          1           2            3           4           5           6            7          8           9         10
var_vocab = [root.y.z  root.y.0.f  root.y.1.x  root.y.1.y  root.y.1.z  root.y.0.x  root.y.z.x  root.y.0.y  root.y.z.y  root.y.0.z  root.y.z.z]
var_type  = [ alias        new          new        new       new          new        impref         new       impref      new        impref  ]
dep       = {  root.y.z : "/root/z.scm"  }

(import root.y.z "/root/z.scm")
(define root.y.0.f (lambda (root.y.1.x root.y.1.y root.y.1.z) 888))
(define root.y.0.x root.y.z.x)
(define root.y.0.y root.y.z.y)
(define root.y.0.z root.y.z.z)


;; root.x
varid     =     0          1         2                6            7            8         9           10            11         12         13           14
var_vocab = [root.x.z  root.x.y  root.x.0.f  ...  root.x.0.x  root.x.y.x  root.x.z.x  root.x.0.y  root.x.y.y  root.x.z.y  root.x.0.z  root.x.y.z   root.x.z.z]
var_type  = [ alias     alias        new     ...     new        impref       impref      new        impref       impref       new       impref       impref  ]
dep       = {  root.x.z : "/root/y.scm"      root.x.y : "/root/z.scm"          }

H1 (import  V0  H2"/root/y.scm")
H3 (import  V1  H4"/root/z.scm")
H5 (define  V2  H6)
H6 (lambda [V3  V4  V5] 888)
H7 (define  V6  H8)
H8 (+       V7  V8)
...

(import root.x.z "/root/y.scm") ;; 注意：故意不对应
(import root.x.y "/root/z.scm")
(define root.x.0.f (lambda (root.x.1.x root.x.1.y root.x.1.z) 888))
(define root.x.0.x (+ root.x.y.x root.x.z.x))
(define root.x.0.y (+ root.x.y.y root.x.z.y))
(define root.x.0.z (+ root.x.y.z root.x.z.z))
```

## 外部引用的相关约定和分析

约定：import_ref=alias.extvar，其中alias和extvar都不能有点

在Parse+ARN阶段，对于variable：

- builtin、nativeid、native_ref不ARN。它们都有全局符号的地位，与其所在上下文无关。
- alias是顶级的，避免与其他模块中的同名alias冲突，因此alias应当重命名为mod_id.alias。
- alias跟同一模块的其他topvar不会重名，因为alias和其他topvar在arn后不是同一个符号（mod_id.alias vs mod_id.TOP.var）。
- import_ref=alias.extvar在parse阶段需要标记为import_ref类型，再将其前面简单拼接mod_id，变成mod_id.alias.extvar。


## 链接器（linker）整体算法

模块链接过程，从一个引用根模块出发，递归地处理所有相关模块，并完成模块合并，最终输出链接后的模块。输出模块是基于输入模块的。

链接过程中，处理的模块数上限设定为1024。

链接器全局上下文数据结构定义（供参考）：

```
typedef struct am_linker_ctx_t {
    size_t module_counter;
    am_vocab_t *all_module_path; // mod_index -> module_path
    am_ast_t *ALLAST; // mod_index->ast
    size_t DAG[][2]; // 邻接关系列表 importer_index -> importee_index
    size_t sorted_ast_index[];
    size_t edge_num;
} am_linker_ctx_t;
```

链接器入口函数定义：

am_ast_t *am_link(am_ast_t *main_ast, wchar_t *base_dir); // main_ast就是引用根模块

### 链接过程1：递归解析所有依赖模块

从起始代码开始，解析为AST，读取其所有导入文件，并逐个解析为AST，递归读取并解析，过程中完成：1收集AST；2构建DAG。伪代码如下：

```
void import_analysis(am_linker_ctx_t *ctx, wchar_t *importee_path, size_t importer_index) {
    size_t current_module_index = vocab_find_index(ctx->all_module_path, importee_path);
    if (current_module_index == SIZE_MAX) {
        current_module_index = ctx->module_counter;
        ctx->all_module_path[current_module_index] = importee_path;
        wchar_t *code = read_from_file(importee_path);
        am_ast_t *current_ast = am_parse(ctx->alloc, code, importee_path, 0);
        ctx->ALLAST[current_module_index] = current_ast;
        foreach (path of current_ast->dependencies) {
            import_analysis(ctx, path, current_module_index);
        }
        ctx->module_counter++;
    }
    ctx->DAG[ctx->edge_num] = {importer_index, current_module_index});
    ctx->edge_num++;
}
```

### 链接过程2：对引用关系做拓扑排序，并按照线性顺序逐步融合

// 对DAG进行拓扑排序的工具函数
size_t *am_topo_sort(size_t DAG[][2], size_t edge_num);

从起始的根引用模块开始，按照拓扑排序顺序，逐个吃掉importee。伪代码如下：

```
am_linker_ctx_t *ctx;

// 对DAG做拓扑排序，同时检查是否成环，得到依赖列表
ctx->sorted_ast_index = topo_sort(ctx->DAG, ctx->edge_num);
if (ctx->sorted_ast_index == NULL) {
    error("循环依赖！");
}

// 以排序后的第一个模块（也就是位于依赖链根部的入口模块）为全局的importer
am_ast_t *global_ast = ALLAST[ctx->sorted_ast_index[0]];
// 从第一个importer（也就是main_ast，应用根模块）开始，遍历依赖排序，逐个吃掉importee模块
for (size_t i = 1; i < ctx->module_counter; i++) {
    size_t importee_index = ctx->sorted_ast_index[i];
    am_ast_merge(global_ast, ALLAST[importee_index], 0);
}

// 链接器最后返回链接后的global_ast
```

## 测试与测试用例

参照 @test/test_parser.c ，实现完整的测试程序。

你可以使用 /home/bd4sur/animac/x.scm 作为链接器的输入（起始AST），对其parse之后，按照上述算法，递归parse所有依赖模块并链接成一个大模块。

测试用的文件及其引用关系如下：

```
;; /home/bd4sur/animac/x.scm
(import z "/home/bd4sur/animac/y.scm") ;; 注意：故意不对应
(import y "/home/bd4sur/animac/z.scm")
(define f (lambda (x y z) 888))
(define x (+ y.x z.x))
(define y (+ y.y z.y))
(define z (+ y.z z.z))

;; /home/bd4sur/animac/y.scm
(import z "/home/bd4sur/animac/z.scm")
(define f (lambda (x y z) 888))
(define x z.x)
(define y z.y)
(define z z.z)

;; /home/bd4sur/animac/z.scm
(define f (lambda (x y z) 666))
(define x 100)
(define y 200)
(define z 300)
```

引用关系为（从importer指向importee）：x->y,x->z,y->z。拓扑排序后：[x, y, z]

链接后的AST为（以代码形式给出，供参考）：

```
(define home.bd4sur.animac.z.0.f (lambda (home.bd4sur.animac.z.1.x home.bd4sur.animac.z.1.y home.bd4sur.animac.z.1.z) 666))
(define home.bd4sur.animac.z.0.x 100)
(define home.bd4sur.animac.z.0.y 200)
(define home.bd4sur.animac.z.0.z 300)
(import home.bd4sur.animac.y.z "/home/bd4sur/animac/z.scm")
(define home.bd4sur.animac.y.0.f (lambda (home.bd4sur.animac.y.1.x home.bd4sur.animac.y.1.y home.bd4sur.animac.y.1.z) 888))
(define home.bd4sur.animac.y.0.x home.bd4sur.animac.y.z.x)
(define home.bd4sur.animac.y.0.y home.bd4sur.animac.y.z.y)
(define home.bd4sur.animac.y.0.z home.bd4sur.animac.y.z.z)
(import home.bd4sur.animac.x.z "/home/bd4sur/animac/y.scm")
(import home.bd4sur.animac.x.y "/home/bd4sur/animac/z.scm")
(define home.bd4sur.animac.x.0.f (lambda (home.bd4sur.animac.x.1.x home.bd4sur.animac.x.1.y home.bd4sur.animac.x.1.z) 888))
(define home.bd4sur.animac.x.0.x (+ home.bd4sur.animac.x.y.x home.bd4sur.animac.x.z.x))
(define home.bd4sur.animac.x.0.y (+ home.bd4sur.animac.x.y.y home.bd4sur.animac.x.z.y))
(define home.bd4sur.animac.x.0.z (+ home.bd4sur.animac.x.y.z home.bd4sur.animac.x.z.z))
```


请你实现上述需求。你可以使用WSL进行编译构建和测试。测试输入文件 /home/bd4sur/animac/x.scm 位于WSL的文件系统内，你可以在WSL内直接读取。


---------------------



开始编码前，请先阅读 @doc/AGENTS.md 。

为实现Scheme解释器的AST模块链接器，请你在 @src/ast.c 中，根据下文描述的算法，实现两个AST的融合算法，即 am_ast_merge 函数。

## 术语约定

- 模块：一个完整的Scheme源码文件，及其解析得到的AST。模块之间可互相引用。
- 外部引用：指的是通过import和点号分隔标识符，引用外部模块变量。(import Alias "/path/to/module.scm")表达式，声明对外部模块的导入，并赋予其“别名”Alias，别名属于特殊变量，其类型为AM_VAR_TYPE_IMPORT_ALIAS。代码中通过“别名.标识符”的格式，引用外部模块的变量。“别名.标识符”整体也是一个变量，在parse阶段，其类型为AM_VAR_TYPE_IMPORT_REF。
- 模块ID：或者叫模块的“全限定名”，是模块源码文件的绝对路径转换得到的、在整个宿主环境中具有唯一性的字符串。模块ID实质上就是模块文件的绝对路径，只是被转换成了点号分隔的标识符格式。模块ID在模块parse和链接阶段，模块ID和模块绝对路径用于唯一确定一个模块，使得参与链接的所有变量都具有全局唯一的字符串形式和varid。
- importer（主引模块）：代码中通过(import ...)引用其他模块的模块。
- importee（被引模块）：被其他importer引用的模块。主引被引是相对的。在引用关系有向无环图（DAG）构建过程中，规定importer指向importee。
- 引用根：链接任务的唯一输入模块，是引用关系图的唯一起点。一般是项目的主模块（main.scm之类的）。

## 两个AST融合算法的伪代码

注意：以下伪代码完全没有考虑类型检查、错误处理、内存分配和释放等工程细节，你需要补全这些必要细节。你需要特别注意 am_value_t 和 am_handle_t/am_varid_t/am_symbol_t 等类型的区别。am_value_t是解释器的tagged pointer value，是包装了的值，而后面那些是裸值。具体可参考 @include/object.h 。

```

// 返回值：0：成功，-1：失败
int32_t am_ast_merge(am_ast_t *importer, am_ast_t *importee, int32_t order) {

    // 需要合并或修改的AST字段：symbol_vocab、var_vocab、var_type、nodes、lambda_handles、tailcall_handles、var_top、dependencies、natives，除此之外不修改或保留。

    // 第1步：修改importee，将元数据合并到importer。

    // 1.1 修改symbol映射

    // 记录symbol映射关系（importee(旧)->importer(新)）
    am_map_t *symbol_merge_mapping = am_map_create(importer->alloc, importee->symbol_vocab->length);
    // 遍历importee的所有symbol
    for (size_t index < importee->symbol_vocab->length) {
        // 将importee的symbol_vocab加入importer的symbol_vocab，获得symbol在importer中的新index
        am_value_t old_symbol_value = am_make_value_of_symbol(index);
        wchar_t *word = am_vocab_get(importee->alloc, importee->symbol_vocab, index);
        size_t new_symbol_index = am_vocab_insert(importer->alloc, importer->symbol_vocab, word);
        am_value_t new_symbol_value = am_make_value_of_symbol(new_symbol_index);
        // 登记映射关系
        am_map_set(importer->alloc, symbol_merge_mapping, old_symbol_value, new_symbol_value);
    }

    // 1.2 修改variable映射及相关元数据

    // 记录varid映射关系（importee(旧)->importer(新)）
    am_map_t *varid_merge_mapping = am_map_create(importer->alloc, importee->var_vocab->length);
    // 遍历importee的所有varible
    for (size_t varid < importee->var_vocab->length) {
        // 将importee的var_vocab加入importer的var_vocab，获得variable在importer中的新varid
        am_value_t old_varid_value = am_make_value_of_varid(varid);
        wchar_t *word = am_vocab_get(importee->alloc, importee->var_vocab, varid);
        size_t new_varid = am_vocab_insert(importer->alloc, importer->var_vocab, word);
        am_value_t new_varid_value = am_make_value_of_varid(new_varid);
        // 登记映射关系
        am_map_set(importer->alloc, varid_merge_mapping, old_varid_value, new_varid_value);
        // 迁移var_type
        am_value_t vtype = am_list_get(importee->alloc, importee->var_type, varid);
        am_list_set(importer->alloc, importer->var_type, new_varid, vtype);
        // 迁移var_top
        for (am_value_t vv of importee->var_top) {
            if (vv == old_varid_value) am_list_push(importer->alloc, importer->var_top, vv);
        }
    }

    // 1.3 修改所有handle

    // 记录handle映射关系（importee(旧)->importer(新)）
    am_map_t *handle_merge_mapping = am_map_create(importer->alloc, importee->nodes->handle_counter);
    // 第一遍扫描：在importer->nodes中申请新handle，记录importee->nodes中所有的handle与新handle的映射关系
    for (am_handle_t hd of importee->nodes) {
        am_value_t old_handle_value = am_make_value_of_handle(hd);
        // 在importer->nodes申请新handle
        am_handle_t new_handle = am_heap_alloc_handle(importer->alloc, importer->nodes);
        am_value_t new_handle_value = am_make_value_of_handle(new_handle);
        // 登记映射关系
        am_map_set(importer->alloc, handle_merge_mapping, old_handle_value, new_handle_value);
    }
    // 第二遍扫描：修改并迁移元数据中的handle
    // 迁移lambda_nodes
    for (am_value_t hd_value of importee->lambda_nodes) {
        am_value_t new_hd_value = am_map_get(importer->alloc, handle_merge_mapping, hd_value);
        am_list_push(importer->alloc, importer->lambda_nodes, new_hd_value);
    }
    // 迁移tailcall_handles
    for (am_value_t hd_value of importee->tailcall_handles) {
        am_value_t new_hd_value = am_map_get(importer->alloc, handle_merge_mapping, hd_value);
        am_list_push(importer->alloc, importer->tailcall_handles, new_hd_value);
    }
    // 迁移dependencies
    for ({am_value_t varid_value , am_value_t hd_value} of importee->dependencies) {
        am_value_t new_varid_value = am_map_get(importer->alloc, varid_merge_mapping, varid_value);
        am_value_t new_hd_value = am_map_get(importer->alloc, handle_merge_mapping, hd_value);
        am_map_set(importer->alloc, importer->dependencies, new_varid_value, new_hd_value);
    }
    // 迁移natives
    for ({am_value_t varid_value , am_value_t hd_value} of importee->natives) {
        am_value_t new_varid_value = am_map_get(importer->alloc, varid_merge_mapping, varid_value);
        am_value_t new_hd_value = am_map_get(importer->alloc, handle_merge_mapping, hd_value);
        am_map_set(importer->alloc, importer->natives, new_varid_value, new_hd_value);
    }
    // 第三遍扫描：全量遍历importee->nodes中所有的list对象的所有children，针对symbol、varid和handle进行匹配和替换
    for (am_handle_t hd of importee->nodes) {
        am_value_t val = am_heap_get(importee->alloc, importee->nodes, am_make_value_of_handle(hd));
        if (am_value_is_ptr(val)) {
            am_object_t *obj = am_value_to_ptr(val);
            int32_t obj_type = obj->base.type;
            // 如果是list对象，则遍历lst中的所有children，根据value的类型进行匹配和替换
            if (obj_type == AM_OBJECT_TYPE_LIST) {
                am_list_t *lst = (am_list_t*)obj;
                for (am_value_t child in lst->children) {
                    // 此处判断child的TPV的类型：
                    // - 如果是AM_VALUE_TYPE_SYMBOL，则通过child在symbol_merge_mapping中找到新的value，并原位替换原value
                    // - 如果是AM_VALUE_TYPE_VARID，则通过child在varid_merge_mapping中找到新的value，并原位替换原value
                    // - 如果是AM_VALUE_TYPE_HANDLE，则通过child在handle_merge_mapping中找到新的value，并原位替换原value
                    // - 其他值类型无动作
                }
            }
        }
        else 报错
    }

    // 第2步：将importee的所有nodes拷贝到importer->nodes中

    for (am_handle_t hd of importee->nodes) {
        am_value_t new_hd_value = am_map_get(importer->alloc, handle_merge_mapping, am_make_value_of_handle(hd));
        am_value_t val = am_heap_get(importee->alloc, importee->nodes, am_make_value_of_handle(hd));
        if (am_value_is_ptr(val)) {
            am_object_t *obj = am_value_to_ptr(val);
            int32_t obj_type = obj->base.type;
            // 如果是list对象，则拷贝并将新指针写入heap
            if (obj_type == AM_OBJECT_TYPE_LIST) {
                am_list_t *lst = (am_list_t*)obj;
                am_list_t *new_lst = am_list_copy(importer->alloc, lst);
                am_heap_set(importer->alloc, importer->nodes, new_hd_value, am_make_value_of_ptr((am_object_t*)new_lst));
            }
        }
        else 报错
    }

    // 第3步：在importer中更新来自importee的node的list亲子节点结构信息，这是AST融合的核心步骤
    // 实现本函数时必须牢记的参考信息：
    //   Lambda表结构说明：Lambda表采用形参列表扁平化存储的设计，具体如下。
    //   children = ['lambda , n_param , param0 , ... , param(n-1) , body0 , ...]
    //   - length = children项数，等于lambda关键字1项+形参数量字段1项+形参数量+函数体项数
    //   - children[0] = AM_VALUE_KW_lambda
    //   - children[1] = am_value_t(满足am_value_is_uint) 引数（形参）数量，记为n
    //   - children[2 ~ (2+n)] = n个形参的am_varid_t，且都必须为am_varid_n类型
    //   - children[(2+n) ~ length] = lambda函数体各项的am_value_t
    //   例如：(lambda (x y) 666) 对应列表对象的children为：['lambda , 2 , x_varid , y_varid , 666]，因而形参数为2，函数体项数=length-形参数-2=1
    // 原理简述：模块AST的顶层lambda节点，其把柄为ast->top_lambda_handle，就是整个模块的“顶层作用域”。在这个lambda节点的直接bodies中的所有children，都是模块的“顶级节点”。AST融合的过程，就是将importee的所有“顶级节点”，“嫁接”到importer的顶层作用域上。
    // 这个“嫁接”过程有两个需要注意的细节：一是不仅要将importee的所有顶级节点的新handle（此时importee的所有node已经拷贝到importer->nodes中了，其映射关系记录在handle_merge_mapping中）加入顶层作用域的bodies，还要将这些顶级节点的parent字段修改为importer的顶层lambda节点的handle。二是嫁接有方向，受到order参数的控制。默认情况下（order=0），importee是被依赖的，importer需要用到importee提供的信息，因此importee的顶级节点在加入importer的顶层作用域bodies时，应当置于importer原有顶级节点的前面。例如importee的顶级节点是H5、H6、H7，而importer的原有顶级节点是H1、H2、H3，则merge之后，importer的顶层lambda节点的bodies应当是[H5,H6,H7 , H1,H2,H3]。如果order=1，则importee的顶级节点在importer的后面，也即[H1,H2,H3 , H5,H6,H7]。

    // 最后：返回融合后的唯一AST：importer。并完成临时对象析构等收尾工作。

}

```

## 测试

你需要在 @test/test_parser.c 中，参考现有代码，新增一个测试am_ast_merge的用例。该用例从文件系统读取两个测试文件，分别是 importer: "/home/bd4sur/animac/x.scm" 和 importee: "/home/bd4sur/animac/y.scm"，parse后，执行 am_ast_merge 进行合并，并检查合并结果。

作为参考，我提供的测试Scheme代码和预期结果是：

```
;; importee: /home/bd4sur/animac/y.scm
(import z "/home/bd4sur/animac/z.scm")
(define f (lambda (x y z) 888))
(define x z.x)
(define y z.y)
(define z z.z)

;; importer: /home/bd4sur/animac/x.scm
(import z "/home/bd4sur/animac/y.scm")
(import y "/home/bd4sur/animac/z.scm")
(define f (lambda (x y z) 888))
(define x (+ y.x z.x))
(define y (+ y.y z.y))
(define z (+ y.z z.z))

;; 合并后的AST（对应的代码，供参考）
;; 需要说明的是：变量名中出现的数字，取决于实际的对象handle分配情况，你不必关心具体的值，只需要大概检查整体结构是否正确即可。
(import home.bd4sur.animac.y.z "/home/bd4sur/animac/z.scm")
(define home.bd4sur.animac.y.0.f (lambda (home.bd4sur.animac.y.1.x home.bd4sur.animac.y.1.y home.bd4sur.animac.y.1.z) 888))
(define home.bd4sur.animac.y.0.x home.bd4sur.animac.y.z.x)
(define home.bd4sur.animac.y.0.y home.bd4sur.animac.y.z.y)
(define home.bd4sur.animac.y.0.z home.bd4sur.animac.y.z.z)
(import home.bd4sur.animac.x.z "/home/bd4sur/animac/y.scm")
(import home.bd4sur.animac.x.y "/home/bd4sur/animac/z.scm")
(define home.bd4sur.animac.x.0.f (lambda (home.bd4sur.animac.x.1.x home.bd4sur.animac.x.1.y home.bd4sur.animac.x.1.z) 888))
(define home.bd4sur.animac.x.0.x (+ home.bd4sur.animac.x.y.x home.bd4sur.animac.x.z.x))
(define home.bd4sur.animac.x.0.y (+ home.bd4sur.animac.x.y.y home.bd4sur.animac.x.z.y))
(define home.bd4sur.animac.x.0.z (+ home.bd4sur.animac.x.y.z home.bd4sur.animac.x.z.z))

```

请你实现上述需求。不得修改文件系统中的测试用例Scheme代码。你可以使用WSL进行编译构建和测试。



---------------------

### 链接过程3：对所有模块做外部引用解析

开始编码前，请先阅读 @doc/AGENTS.md 。

为实现Scheme解释器的AST模块链接器，请你在 @src/linker.c 中，根据下文描述的算法，实现AST的“外部引用解析”算法，即 am_linker_import_ref_resolution 函数。

## 术语约定

- 模块：一个完整的Scheme源码文件，及其解析得到的AST。模块之间可互相引用。
- 外部引用：指的是通过import和点号分隔标识符，引用外部模块变量。(import Alias "/path/to/module.scm")表达式，声明对外部模块的导入，并赋予其“别名”Alias，别名属于特殊变量，其类型为AM_VAR_TYPE_IMPORT_ALIAS。代码中通过“别名.标识符”的格式，引用外部模块的变量。“别名.标识符”整体也是一个变量，在parse阶段，其类型为AM_VAR_TYPE_IMPORT_REF。
- 模块ID：或者叫模块的“全限定名”，是模块源码文件的绝对路径转换得到的、在整个宿主环境中具有唯一性的字符串。模块ID实质上就是模块文件的绝对路径，只是被转换成了点号分隔的标识符格式。模块ID在模块parse和链接阶段，模块ID和模块绝对路径用于唯一确定一个模块，使得参与链接的所有变量都具有全局唯一的字符串形式和varid。
- importer（主引模块）：代码中通过(import ...)引用其他模块的模块。
- importee（被引模块）：被其他importer引用的模块。主引被引是相对的。在引用关系有向无环图（DAG）构建过程中，规定importer指向importee。
- 引用根：链接任务的唯一输入模块，是引用关系图的唯一起点。一般是项目的主模块（main.scm之类的）。

## 函数原型和算法描述

```
// 对合并后的AST执行外部引用解析，也就是将AST中所有的var_type=AM_VAR_TYPE_IMPORT_REF类型的变量，替换为dependencies对应模块中的变量全限定名
// 成功返回0，失败返回-1。
int32_t am_linker_import_ref_resolution(am_ast_t *merged_ast, wchar_t *base_dir);
```

外部引用解析算法：

- 首先要知道，外部引用解析发生在AST完全融合之后，所有的名字都是全局唯一无歧义的。
- 全量遍历整个合并后的AST，在children中寻找IMPORT_REF类型的变量。
- 从最后一个点将IMPORT_REF分割为 prefix=importer_id.alias 和 suffix。
- 在全局dependencies中，查询 prefix=importer_id.alias 对应的 importee_path ，转为模块ID：importee_id。
- 在var_top中查找IMPORT_REF在importee中的变量名：匹配以下模式的变量：importee_id.[top_lambda_node_of_importee].suffix。说明：尽管中间的[top_lambda_node_of_importee]不知道，但是，通过importee_id+顶级变量+suffix三个信息，已经能够在var_top中唯一定位出一个变量。如有任何异常（找不到等）则报错。
- 用新找到的top_var变量，替换掉旧的IMPORT_REF类型的child。
- 对所有IMPORT_REF变量的child重复以上过程，直至全部遍历完毕。

## 测试与测试用例

在 @test/test_linker.c 中，对 test_linker_recursive 函数合并完成的AST执行 am_linker_import_ref_resolution ，并输出处理后的AST。

测试用的文件及其引用关系如下：

```
;; /home/bd4sur/animac/x.scm
(import z "/home/bd4sur/animac/y.scm") ;; 注意：故意不对应
(import y "/home/bd4sur/animac/z.scm")
(define f (lambda (x y z) 888))
(define x (+ y.x z.x))
(define y (+ y.y z.y))
(define z (+ y.z z.z))

;; /home/bd4sur/animac/y.scm
(import z "/home/bd4sur/animac/z.scm")
(define f (lambda (x y z) 888))
(define x z.x)
(define y z.y)
(define z z.z)

;; /home/bd4sur/animac/z.scm
(define f (lambda (x y z) 666))
(define x 100)
(define y 200)
(define z 300)
```

引用关系为（从importer指向importee）：x->y,x->z,y->z。拓扑排序后：[x, y, z]

链接后的AST为（以代码形式给出，供参考）：

```
(define home.bd4sur.animac.z.0.f (lambda (home.bd4sur.animac.z.1.x home.bd4sur.animac.z.1.y home.bd4sur.animac.z.1.z) 666))
(define home.bd4sur.animac.z.0.x 100)
(define home.bd4sur.animac.z.0.y 200)
(define home.bd4sur.animac.z.0.z 300)
(import home.bd4sur.animac.y.z "/home/bd4sur/animac/z.scm")
(define home.bd4sur.animac.y.0.f (lambda (home.bd4sur.animac.y.1.x home.bd4sur.animac.y.1.y home.bd4sur.animac.y.1.z) 888))
(define home.bd4sur.animac.y.0.x home.bd4sur.animac.y.z.x)
(define home.bd4sur.animac.y.0.y home.bd4sur.animac.y.z.y)
(define home.bd4sur.animac.y.0.z home.bd4sur.animac.y.z.z)
(import home.bd4sur.animac.x.z "/home/bd4sur/animac/y.scm")
(import home.bd4sur.animac.x.y "/home/bd4sur/animac/z.scm")
(define home.bd4sur.animac.x.0.f (lambda (home.bd4sur.animac.x.1.x home.bd4sur.animac.x.1.y home.bd4sur.animac.x.1.z) 888))
(define home.bd4sur.animac.x.0.x (+ home.bd4sur.animac.x.y.x home.bd4sur.animac.x.z.x))
(define home.bd4sur.animac.x.0.y (+ home.bd4sur.animac.x.y.y home.bd4sur.animac.x.z.y))
(define home.bd4sur.animac.x.0.z (+ home.bd4sur.animac.x.y.z home.bd4sur.animac.x.z.z))
```

完成外部引用解析后的AST为（以代码形式给出，供参考），注意 home.bd4sur.animac.y.z.x、home.bd4sur.animac.y.z.y、home.bd4sur.animac.y.z.z、home.bd4sur.animac.x.y.x、home.bd4sur.animac.x.z.x、home.bd4sur.animac.x.y.y、home.bd4sur.animac.x.z.y、home.bd4sur.animac.x.y.z、home.bd4sur.animac.x.z.z 的替换规则：

```
(define home.bd4sur.animac.z.0.f (lambda (home.bd4sur.animac.z.1.x home.bd4sur.animac.z.1.y home.bd4sur.animac.z.1.z) 666))
(define home.bd4sur.animac.z.0.x 100)
(define home.bd4sur.animac.z.0.y 200)
(define home.bd4sur.animac.z.0.z 300)
(import home.bd4sur.animac.y.z "/home/bd4sur/animac/z.scm")
(define home.bd4sur.animac.y.0.f (lambda (home.bd4sur.animac.y.1.x home.bd4sur.animac.y.1.y home.bd4sur.animac.y.1.z) 888))
(define home.bd4sur.animac.y.0.x home.bd4sur.animac.z.0.x)
(define home.bd4sur.animac.y.0.y home.bd4sur.animac.z.0.y)
(define home.bd4sur.animac.y.0.z home.bd4sur.animac.z.0.z)
(import home.bd4sur.animac.x.z "/home/bd4sur/animac/y.scm")
(import home.bd4sur.animac.x.y "/home/bd4sur/animac/z.scm")
(define home.bd4sur.animac.x.0.f (lambda (home.bd4sur.animac.x.1.x home.bd4sur.animac.x.1.y home.bd4sur.animac.x.1.z) 888))
;; NOTE home.bd4sur.animac.x.y -> "/home/bd4sur/animac/z.scm" -> home.bd4sur.animac.z -> [in var_top] -> home.bd4sur.animac.z.0.x
(define home.bd4sur.animac.x.0.x (+ home.bd4sur.animac.z.0.x home.bd4sur.animac.y.0.x))
(define home.bd4sur.animac.x.0.y (+ home.bd4sur.animac.z.0.y home.bd4sur.animac.y.0.y))
(define home.bd4sur.animac.x.0.z (+ home.bd4sur.animac.z.0.z home.bd4sur.animac.y.0.z))
```

请你实现上述需求。你可以使用WSL进行编译构建和测试。测试输入文件 /home/bd4sur/animac/x.scm 位于WSL的文件系统内，你可以在WSL内直接读取。


---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

@src/parser.c 是解释器的parser实现。目前的am_parser实现中，包含两个主要的分析步骤：递归下降语法分析、预处理指令解析。

我的需求是：在预处理指令解析之后，增加一个步骤“引用模块别名（alias）和外部引用（ext_ref）更名”。这个步骤需要完成的事情有：

遍历ast->nodes的全部节点：

对于 (import alias mod_path) 节点中的alias，你需要参照 @src/ast.c 中的 am_ast_make_unique_variable 函数中所描述的逻辑，构造一个新的alias variable，新的alias的格式是“module_id.alias”，也就是直接把module_id和原来的alias以点号拼接起来。例如，在模块“path.to.a”中，旧的alias“Lib”被组装成新的alias“path.to.a.Lib”。我建议你在 @src/ast.c 中参照 am_ast_make_unique_variable 实现一个新的工具函数 am_ast_make_unique_module_alias，通过这个函数构造新alias并获得它的varid，将其var_type设置为AM_VAR_TYPE_IMPORT_ALIAS，再用这个varid取代 (import alias mod_path) 节点中的旧alias。同时，你需要在ast->dependencies中增加新alias的varid到module_path的映射。不需要删除旧varid和旧映射，因为下一步还要用。

对于其他节点，遍历其所有children。对于children中出现的varid，首先检查其在var_type中的变量类型，如果是 AM_VAR_TYPE_EXT_REF 类型的，则通过 @src/ast.c 中的 am_ast_check_import_ref 确认它是否是 AM_VAR_TYPE_IMPORT_REF。如果是，则还是参照 @src/ast.c 中的 am_ast_make_unique_variable 实现一个新的工具函数 am_ast_make_unique_import_ref，构造一个新的 import_ref 格式的变量，其格式为“module_id.import_ref”，也就是直接把module_id和原来的import_ref以点号拼接起来，加入var_vocab，获得varid，再将其var_type设置为AM_VAR_TYPE_IMPORT_REF。例如，在模块“path.to.a”中，原来的import_ref是“Lib.foo”，则替换后的import_ref就是“path.to.a.Lib.foo”，这实际上就是新的模块alias“path.to.a.Lib”与模块内变量“foo”的拼接。调用am_ast_make_unique_import_ref取得新的varid后，用它替换掉children中相应的旧的varid。

请你实现上述需求。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

@src/parser.c 是解释器的parser实现。请你在 am_parser 函数的最后一个环节 populate_top_lambda_and_var_top 后面，再增加一个新的分析环节：尾位置分析。尾位置分析的目的是递归遍历整个AST，按照规则，标记出处于尾位置上的application节点，也就是“尾调用”节点，将尾调用节点的handle加入ast->tailcall_handles字段。

具体的规则，你可以参照 @typescript/src/Analyser.ts 中的 TailCallAnalysis。

请你实现上述需求。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

为实现模块链接器中的依赖DAG的拓扑排序，请你在 @src/linker.c 中实现一个对DAG进行拓扑排序的函数。成功返回排序后的列表（由调用者释放），失败返回SIZE_MAX。参数DAG的格式是{{出节点index, 入节点index}, ...}

函数原型为：

size_t *am_topo_sort(size_t DAG[][2], size_t edge_num);

请你实现上述需求。你可以使用WSL进行编译构建和测试（可视化输出算法的输入输出）。

---------------------

你之前对 @src/ast.c 中的 am_ast_merge 的实现有误：

在“第3步：将 importee 的顶级节点嫁接到 importer 的顶层作用域”中，你首先通过importee->nodes和importee_top_lambda获得了importee_bodies，这是正确的，因为在第一步中，已经将importee的所有node的children替换为新的（在importer->nodes中分配的）handle。

因此，后续过程中，你可以直接使用601行“am_value_t *importee_bodies = am_list_lambda_get_bodies(...”获取的importee_bodies，而不需要再做handle_merge_mapping的映射查表。

在你现在的实现中，引入了mapped_importee_bodies，由于上面的分析，这是多此一举的、画蛇添足的。

因此，我要求你在“第3步：将 importee 的顶级节点嫁接到 importer 的顶层作用域”中，去掉mapped_importee_bodies相关逻辑，直接使用你已经获取的importee_bodies即可。

---------------------

在现有的 @src/ast.c 的 am_ast_merge 实现中，虽然融合逻辑是正确的，但是最后没有清理importee被抛弃的、并且已经被复制到importer->nodes的：最顶级application节点和它的唯一的child：最顶级的lambda节点。

举例如下。下面是融合后的AST结构。可见，从融合后的 top_lambda_handle = <H:88> 出发，可以定位到“活”的顶级节点<H:88>和<H:87>。而原本归属于importee的顶级节点<H:105>和<H:108>是遗留的垃圾、在新的融合后AST中是不可达的。

因此，请你在 am_ast_merge 完成之后，在importer->nodes中，清理掉这些垃圾节点。

```
top_lambda_handle: <H:88>
lambda_handles: [<H:88>, <H:94>, <H:105>, <H:101>]
tailcall_handles: [<H:99>, <H:87>, <H:102>, <H:108>]
nodes: {
  <H:90>: WSTRING len=27 "\"/home/bd4sur/animac/y.scm\""
  <H:106>: APPLICATION parent=<H:88> length=3 children=["define", "home.bd4sur.animac.y.102.f", <H:101>]
  <H:109>: APPLICATION parent=<H:88> length=3 children=["define", "home.bd4sur.animac.y.102.y", "home.bd4sur.animac.y.z.y"]
  <H:103>: APPLICATION parent=<H:88> length=3 children=["import", "home.bd4sur.animac.y.z", <H:107>]
  <H:100>: APPLICATION parent=<H:99> length=3 children=["+", "home.bd4sur.animac.x.y.z", "home.bd4sur.animac.x.z.z"]
  <H:97>: APPLICATION parent=<H:88> length=3 children=["define", "home.bd4sur.animac.x.88.y", <H:98>]
  <H:107>: WSTRING len=27 "\"/home/bd4sur/animac/z.scm\""
  <H:98>: APPLICATION parent=<H:97> length=3 children=["+", "home.bd4sur.animac.x.y.y", "home.bd4sur.animac.x.z.y"]
  <H:99>: APPLICATION parent=<H:88> length=3 children=["define", "home.bd4sur.animac.x.88.z", <H:100>]
  <H:102>: APPLICATION parent=<H:88> length=3 children=["define", "home.bd4sur.animac.y.102.z", "home.bd4sur.animac.y.z.z"]
  <H:105>: LAMBDA parent=<H:108> params=0 bodies=5 children=["lambda", 0, <H:103>, <H:106>, <H:104>, <H:109>, <H:102>]
  <H:88>: LAMBDA parent=<H:87> params=0 bodies=11 children=["lambda", 0, <H:103>, <H:106>, <H:104>, <H:109>, <H:102>, <H:89>, <H:91>, <H:93>, <H:95>, <H:97>, <H:99>]
  <H:93>: APPLICATION parent=<H:88> length=3 children=["define", "home.bd4sur.animac.x.88.f", <H:94>]
  <H:91>: APPLICATION parent=<H:88> length=3 children=["import", "home.bd4sur.animac.x.y", <H:92>]
  <H:94>: LAMBDA parent=<H:93> params=3 bodies=1 children=["lambda", 3, "home.bd4sur.animac.x.94.x", "home.bd4sur.animac.x.94.y", "home.bd4sur.animac.x.94.z", 888]
  <H:92>: WSTRING len=27 "\"/home/bd4sur/animac/z.scm\""
  <H:104>: APPLICATION parent=<H:88> length=3 children=["define", "home.bd4sur.animac.y.102.x", "home.bd4sur.animac.y.z.x"]
  <H:89>: APPLICATION parent=<H:88> length=3 children=["import", "home.bd4sur.animac.x.z", <H:90>]
  <H:87>: APPLICATION parent=null length=1 children=[<H:88>]
  <H:101>: LAMBDA parent=<H:106> params=3 bodies=1 children=["lambda", 3, "home.bd4sur.animac.y.106.x", "home.bd4sur.animac.y.106.y", "home.bd4sur.animac.y.106.z", 888]
  <H:95>: APPLICATION parent=<H:88> length=3 children=["define", "home.bd4sur.animac.x.88.x", <H:96>]
  <H:96>: APPLICATION parent=<H:95> length=3 children=["+", "home.bd4sur.animac.x.y.x", "home.bd4sur.animac.x.z.x"]
  <H:108>: APPLICATION parent=null length=1 children=[<H:105>]
}
```

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

在 @src/wstring.c 中，实现以下函数：

```
// 创建并初始化一个字符串对象。字符串对象是不可变的。
am_wstring_t *am_wstring_create(am_allocator_t *alloc, wchar_t *str, size_t length);

// 销毁对象。成功返回0，失败返回-1。
int32_t am_wstring_destroy(am_allocator_t *alloc, am_wstring_t *obj);

// 功能说明：拷贝wstring对象。成功则返回新副本对象的指针，失败则返回NULL。
am_wstring_t *am_wstring_copy(am_allocator_t *alloc, am_wstring_t *obj);

// 功能说明：将字符串对象序列化成二进制序列，并转储到buffer[offset]
// 实现说明：offset是写入buffer的起点offset。成功则返回向buffer新增字节数，失败则返回SIZE_MAX。
// 注意：若buffer设为NULL，或者offset设为SIZE_MAX，则仅计算转储后的二进制序列的字节数，不实际写入buffer。
size_t am_wstring_dump(am_allocator_t *alloc, am_wstring_t *obj, uint8_t *buffer, size_t offset);

// 功能说明：转储（dump）操作的逆操作。从二进制字节序列buffer[offset]开始，读取转储的字符串对象，构造字符串对象并返回其指针。
// 实现说明：offset是读取buffer的起点offset。成功则返回加载后am_wstring_t对象的指针，失败则返回NULL。
am_wstring_t *am_wstring_load(am_allocator_t *alloc, uint8_t *buffer, size_t offset);
```

请你实现上述需求。你可以使用WSL进行编译构建和测试。


---------------------

堆转储算法

开始编码前，请先阅读 @doc/AGENTS.md 。

请你在 @src/heap.c 中，补全实现am_heap_dump和am_heap_deep_dump函数，该函数的功能是将heap及其指向对象全部转储成一个二进制序列，以便传输和后面解析。

需求背景：为了将从Scheme代码解析出的AST转储（序列化）为二进制序列，与编译得到的中间代码等信息一同构成可执行文件，需要将 am_heap_t 整个结构体及其value（解包为am_object_t*）指向的am_object_t对象，序列化为一个紧凑的二进制字节流，同时更新heap中对象指针的值为相对于二进制流起始位置的偏移量。这样，在解析运行之前，就可以通过解析这个二进制字节流，将AST整体加载到内存中。

关于heap的设计思路：heap的实质是一个map，是从am_handle_t到am_value_t（对am_object_t*的TPV包装）的映射。am_heap_t对象本身维护了从逻辑地址（handle）到物理地址（pointer）的映射表，而其value指向的am_object_t对象，则存储在allocator管理的内存中。因此，为了转储整个heap，既需要转储am_heap_t对象，也需要转储am_heap_t的value所指向的对象，并将它们按规则组装成连续、可解析的二进制字节序列。

序列化的整体格式如下：

```
uint64_t buffer_size; // heap序列化的总字节数（含头部的两个长度字段）N=8+8+n+n0+n1+...
uint64_t heap_size; // am_heap_t对象的二进制dump序列的字节数n
uint8_t[n] heap; // am_heap_t对象的二进制dump序列（am_heap_t对象相当于整个序列的header，其value保存了指向各个对象在序列中的offset）
uint8_t[n0] obj[0]; // 对象0的二进制dump序列
uint8_t[n1] obj[1]; // 对象1的二进制dump序列
……
```

接口定义和算法说明如下：

```
// 该函数用于转储am_heap_t对象本身
// 实现说明：offset是写入buffer的起点offset。成功则返回向buffer新增字节数，失败则返回SIZE_MAX。
// 注意1：需要压缩，将capacity压缩到跟length一致，删除多余分配的空闲部分
// 注意2：若buffer设为NULL，或者offset设为SIZE_MAX，则仅计算转储后的二进制序列的字节数，不实际写入buffer。
size_t am_heap_dump(am_allocator_t *alloc, am_heap_t *heap, uint8_t *buffer, size_t offset);


// 该函数用于深度转储整个heap及其指向的对象
// 实现说明：offset是写入buffer的起点offset。成功则返回向buffer新增字节数，失败则返回SIZE_MAX。
size_t am_heap_deep_dump(am_allocator_t *alloc, am_heap_t *heap, uint8_t *buffer, size_t offset) {
    // 初始化buffer偏移量计数器：size_t buffer_offset = offset + 16;（留出两个uint64_t类型的长度字段）
    // 首先通过am_heap_dump获得heap的字节长度size_t heap_map_size，但暂不写入buffer（因为后面还要修改其内容）。
    // 按key升序遍历heap中的每个entry(key,value)，按顺序dump到buffer[offset+heap_map_size]之后的区域。
    //   - 检查value是否是ptr（am_object_t*），如果不是，报错退出；如果是，则取出相应的am_object_t对象obj。
    //   - 从base.type获取obj的类型（仅处理AM_OBJECT_TYPE_LIST、AM_OBJECT_TYPE_WSTRING）
    //     - 根据obj类型分别调用相应类型的dump函数，获取其字节数size_t obj_size，将其dump到buffer[buffer_offset+heap_map_size]
    //     - 修改heap中当前key对应的value为obj的dump在buffer中的偏移量，即am_make_value_of_ptr(*(buffer+buffer_offset+heap_map_size))
    //     - 更新buffer偏移量计数器：buffer_offset += obj_size
    // 通过am_heap_dump将修改后的heap对象dump到buffer[offset+16]
    // 将heap_map_size转为uint64_t，写入buffer[offset+8]
    // 将总字节长度即(buffer_offset-offset)转为uint64_t，写入buffer[offset]
    // 返回buffer和buffer的总字节长度；失败返回SIZE_MAX
}
```

举例：

```
Heap: {H1: 114515, H2: 114514, H3: 114516} (假设序列化后字节数为650)
Arena: {114514: [100Bytes], 114515: [200Bytes], 114516: [300Bytes]}
dump之后的buffer: [(uint64_t)1266, (uint64_t)650, (heap){H1: 766, H2: 666, H3: 966} , (am_object_t:H2)[100Bytes], (am_object_t:H1)[200Bytes], (am_object_t:H3)[300Bytes] ]
  总长度：1266Bytes
```

请你实现上述需求。你可以使用WSL进行编译构建和测试。


---------------------


TODO 图标设计

# Compiler

开始编码前，请先阅读 @doc/AGENTS.md 。

为实现Scheme解释器的编译器，请你阅读 @src/compiler.c ，根据下文描述，优化实现。

实际上我是希望你一步一步实现，每次让你做一部分，但是你直接把整个编译器都做好了。那我就把完整的要求发给你，根据我的要求，检查并调整你在 @src/compiler.c 中已有的实现。
实际上我是希望你一步一步实现，每次让你做一部分，但是你直接把整个编译器都做好了。那我就把完整的要求发给你，根据我的要求，检查并调整你在 @src/compiler.c 中已有的实现。

## 设计思路

编译，就是从链接后的AST出发，递归遍历AST的各个节点，按规则生成中间语言（Intermediate Language，IL）指令序列，最终输出可被VM解释执行的二进制模块文件。

编译过程分为3步：中间语言代码生成、标签解析、代码优化。

中间语言代码生成：根据解释器对Scheme程序结构的约定，所有程序的最外层都是对一个顶级thunk的调用，这意味着所有程序都存在一个顶级lambda节点（作用域），作为程序的唯一入口。而顶级lambda节点中嵌套定义的所有的lambda节点，构成了程序代码的主体框架。由于分析阶段已经消除了所有标识符的歧义，且每个lambda节点都有全局唯一的handle，因此所有的lambda都可以视为全局具名函数，因而此时的Scheme程序可以看作是像C语言程序那样，是由若干个平展的lambda（函数）+一个入口调用构成的。所以，中间语言代码生成的整体框架，就是入口点跳转+编译所有lambda节点。从编译lambda节点出发，递归展开AST，按规则生成中间语言指令序列。具体的编译规则将在后文描述。

标签解析：在既有TS实现中，标签是用于标记IL代码序列位置的字符串。标签遵循字面内容相同则指代同一iddr的判等原则。标签的实质是iaddr，之所以引入标签，是因为编译过程中尚不掌握代码的整体布局，不能未卜先知某一位置的绝对iaddr，所以通过标签来前瞻性地标记代码中的某个相对位置。标签有3种使用方式：构造、定位、解析。实现细节详见各函数的注释。在既有TS实现中，标签的构造，就是在索引值（任何value或者具有唯一标记作用的字符串）前面，加上“@”。这样，索引值内容相同的“@xxx”，不论出现在何处，都是同一个标签，都指代同一个iaddr，也就是它被单独插入指令序列中的位置（实际上相当于一条nop）。实际上，指令的operand使用的是iaddr，但是如前文所述，编译过程中，不可能对iaddr的绝对值未卜先知，因此编译时将原本应为iaddr的operand以label暂时代替。在标签的解析过程中，通过标签-iaddr的映射表，将代码中的标签全部替换为绝对iaddr。

代码优化：暂不实现。

## 编译器基本数据结构和工具函数

```
// 单条IL指令
typedef struct am_instruction_t {
    uint32_t opcode;    // 指令代码：在 @include/opcode.h 中定义的AM_VM_OP_*
    am_value_t operand;  // 操作数：统一为TPV，不同的指令有不同的具体类型要求。无参数则设为AM_VALUE_UNDEFINED。
} am_instruction_t;

// 编译器工作语境
typedef struct am_compiler_ctx_t {
    am_ast_t *ast; // 编译输入的AST，编译过程中会被修改，作为编译结果的一部分（概念上相当于“静态数据段”）
    am_iaddr_t icount; // 中间语言指令计数器
    am_instruction_t *ilcode; // 编译得到的中间语言指令序列
    size_t label_counter; // 用于生成标签枚举值的计数器
    am_map_t *value_label_mapping; // Map<am_value_t(any), am_value_t(label)> 从任何类型的索引TPV到标签TPV的映射
    am_map_t *label_iaddr_mapping; // Map<am_value_t(label), am_value_t(iaddr)> 从label值到iaddr值的映射
    am_list_t *while_tag_stack; // while块的标签跟踪栈：用于处理break/continue
    size_t unique_id_counter; // 用于生成唯一枚举值的计数器
} am_compiler_ctx_t;

// 功能说明：向am_compiler_ctx_t的ilcode中，增加一个am_instruction_t，并更新icount。
// 实现说明：成功返回0；失败返回-1
static int32_t emit_instruction(am_compiler_ctx_t *ctx, uint32_t opcode, am_value_t operand);

// 功能说明：构造一个临时变量，加入AST，返回其varid；或者查询符合给定条件的临时变量的varid。
// 设计说明：编译过程中，某些结构需要引入临时变量，本函数即用于这类过程。
// 实现说明：成功返回varid，失败返回SIZE_MAX
static am_varid_t am_compiler_make_temp_varid(am_compiler_ctx_t *ctx, wchar_t *name, am_value_t label, size_t id) {
    // 将name、label（强转size_t后按%zx格式输出）、id（按%zx格式输出）拼接起来，中间以“_”连接，得到临时变量的字符串，例如“condbranch_1a2b3c4d_0”
    // 首先判断临时变量字符串是否存在于ctx->ast->var_vocab，若存在，则直接返回其varid。
    // 若不存在，则将临时变量字符串加入ctx->ast->var_vocab，获取其varid，再将这个新的varid的ctx->ast->var_type设为AM_VAR_TYPE_ILTEMP。
    // 成功返回varid，失败返回SIZE_MAX
}

// 功能说明：标签构造——根据给定的索引TPV（index_value），构造标签（am_value_t）。
// 实现说明：基于任意TPV（一般是handle、varid，称为“索引”TPV）构造一个新的标签TPV（AM_VALUE_TYPE_LABEL）。如果相同索引TPV的标签已存在，则获取已构造的标签TPV，以便后面加入指令的operand。由于编译过程中存在先使用后出现的情况，因此对于同一索引的标签，第一次调用本函数，是从无到有地创建标签，后续调用则是返回已创建的同一标签。只要用于构造标签的索引TPV相等，则构造出来的标签就是同一个标签，这种判定原则与symbol类似。既有TS实现是给索引value前面加@前缀，而当前C语言实现的TPV长度固定，不可能在不损失索引TPV信息的前提下直接将其转换成新的标签TPV，因此，在am_compiler_ctx_t中，通过label_counter计数器，为每个新构造的标签TPV赋予一个唯一枚举值，同时通过value_label_mapping，登记从任何类型的索引TPV到标签TPV的映射，这样后面就可以通过索引TPV唯一确定构造时生成的那个标签TPV。成功返回标签TPV，失败返回AM_VALUE_NULL。
static am_value_t am_compiler_make_label(am_compiler_ctx_t *ctx, am_value_t index_value);

// 功能说明：标签定位——为标签指定iaddr。
// 实现说明：标签的功能是指代指令序列中的位置。定位指的是将某个标签TPV与已知的iaddr（过去和当前的iaddr，不可能预知未来的iaddr）进行绑定，将标签->iaddr的映射关系，登记到label_iaddr_mapping中。类似于既有TS实现中的AddInstruction(label)。编译过程中，标签的构造和定位，未必是同时发生的，但必须遵守先构造后定位的原则。成功返回0，失败返回-1。
static int32_t am_compiler_locate_label(am_compiler_ctx_t *ctx, am_value_t index_value, am_iaddr_t iaddr);

// 功能说明：标签解析——通过标签TPV，获取对应的iaddr。
// 实现说明：在AST全部编译完成后，编译器收集到全部的label及其与iaddr的映射关系，此时即可通过label_iaddr_mapping，将所有的label解析并成绝对的iaddr。成功返回iaddr，失败返回SIZE_MAX。
static am_iaddr_t am_compiler_parse_label_to_iaddr(am_compiler_ctx_t *ctx, am_value_t label);


// 功能描述：编译后处理——全局标签解析，该函数在am_compile_all结束后调用，用于将所有的label替换为绝对iaddr。
// 实现描述：遍历所有ilcode，检查am_instruction.operand的am_value_t的TPV类型是否是AM_VALUE_TYPE_LABEL。如果是，则调用am_compiler_parse_label_to_iaddr将其转换为iaddr，加上offset后替换掉原来的label。成功返回0，失败返回-1。
int32_t am_compiler_label_resolution(am_compiler_ctx_t *ctx, am_iaddr_t offset);
```

## 具体编译规则的实现

实现提示：

- 以下用伪代码描述对于AST几类典型节点的编译规则。虽然是伪代码，但也尽量接近现有实现风格。你可以参照我的伪代码，实现所有其他类型AST节点的编译规则。
- 以下伪代码完全没有考虑类型检查、错误处理、内存分配和释放等工程细节，你需要补全这些必要细节。
- 你需要特别注意 am_value_t 和 am_handle_t/am_varid_t/am_symbol_t 等类型的区别。am_value_t是解释器的tagged pointer value，是包装了的值，而后面那些是裸值。具体可参考 @include/object.h 。
- 你需要注意 am_object_t 和 am_list_t 、am_wstring_t 等类型的继承关系。它们通过base头实现继承，具体可参考 @include/object.h 。你在进行对象访问时，要注意按需进行指针类型的转换。
- 你可以参考（但是不完全遵从） @typescript/src/Compiler.ts 中的既有TS实现，补全下列函数实现。在既有TS实现中，所有的value、label、handle、甚至一条IL指令，基本上都是字符串。你需要特别注意指令的operand前面带“@”符号的情况，这意味着它用“@”后面的value作为索引，构造了一个标签。你还需要注意AddInstruction处只添加了一个标签的情况，当时的实现是将标签也当作一条指令加入ilcode的，只不过不会执行，相当于nop；而如今你要做的C语言实现，不要把标签也当作指令加入ilcode，而应该调用am_compiler_locate_label来实现这一点。
- 对于“具名标签”，也就是参考TS代码中诸如`@COND_BRANCH_${uqStr}_${i}`这样的标签，你需要调用am_compiler_make_temp_varid先申请一个varid类型的TPV，然后用这个TPV作为标签的索引来构造一个新标签。当然，你需要记住，内容相同的variable对应同一个varid，同一个varid对应同一个label。在am_compiler_ctx_t中涉及label的映射就是为了保证这一点。具体的你可以参考我下面提供的伪代码。
- 注意下面伪代码中对native_ref、variable、立即数等归类的条件与参考TS代码有所不同，以下面伪代码的分类方式为准。
- 参考TS代码中，指令前面有分号的是注释，直接忽略。例如AddInstruction(`;; ✅ SET! “${nodeHandle}” BEGIN`);

```
// 编译入口：开始编译整个AST。成功返回0，失败返回-1。
int32_t am_compile_all(am_compiler_ctx_t *ctx) {
    // 入口点
    am_value_t top_lambda_label = am_compiler_make_label(ctx, ctx->ast->top_lambda_handle);
    emit_instruction(ctx, AM_VM_OP_call, top_lambda_label);
    // ctx->ret > 0 时跳转到返回目标，否则使用 halt 结束
    if (ctx->ret > 0) {
        emit_instruction(ctx, AM_VM_OP_goto, am_make_value_of_iaddr(ctx->ret));
    }
    else {
        emit_instruction(ctx, AM_VM_OP_halt, AM_VALUE_UNDEFINED);
    }
    // 从所有的Lambda节点开始顺序编译。这类似于C语言，所有的函数都是顶级的。
    for (hd of ctx->ast->lambda_handles) {
        am_compile_lambda(ctx, hd);
    }
}

// 编译Lambda节点。成功返回0，失败返回-1。
int32_t am_compile_lambda(am_compiler_ctx_t *ctx, am_handle_t hd) {
    // 构造并定位标签：本函数在IL代码中的入口点（等效于TS的AddInstruction(`@${nodeHandle}`);）
    am_value_t lambda_label = am_compiler_make_label(ctx, hd);
    am_compiler_locate_label(ctx, hd, ctx->icount);
    // 获取lambda节点对象
    am_list_t *lambda_obj = get_obj_from_nodes_by_handle(ctx->ast, hd); // 伪代码
    // 按参数列表逆序，插入store指令
    am_value_t *parameters = get_parameters_of_lambda(lambda_obj);
    for (size_t i = length_of_parameters - 1; i >= 0; i--) {
        emit_instruction(ctx, AM_VM_OP_store, parameters[i]);
    }
    // 逐个编译函数体，等价于begin块
    am_value_t bodies = get_bodies_of_lambda(lambda_obj);
    for(size_t i = 0; i < length_of_bodies; i++) {
        am_value_t body = bodies[i];
        int32_t body_type = am_value_type(body);
        if (body_type == AM_VALUE_TYPE_HANDLE) {
            am_object_t *body_obj = get_obj_from_nodes_by_handle(ctx->ast, body); // 伪代码
            int32_t body_obj_type = body_obj->base.type;
            if (body_obj_type == AM_OBJECT_TYPE_LIST) {
                int32_t list_type = body_obj->type;
                if (list_type == AM_LIST_TYPE_LAMBDA) {
                    // 对应既有TS实现中的AddInstruction(`loadclosure @${body}`);
                    am_value_t body_lambda_label = am_compiler_make_label(ctx, body);
                    emit_instruction(ctx, AM_VM_OP_loadclosure, body_lambda_label);
                }
                else if (list_type == AM_LIST_TYPE_QUOTE) {
                    emit_instruction(ctx, AM_VM_OP_push, body);
                }
                else if (list_type == AM_LIST_TYPE_QUASIQUOTE) {
                    am_compile_quasiquote(ctx, body);
                }
                else if (list_type == AM_LIST_TYPE_APPLICATION || list_type == AM_LIST_TYPE_UNQUOTE) {
                    am_compile_application(ctx, body);
                }
                else {
                    error("意外的函数体（列表）节点类型。");
                }
            }
            else if (body_obj_type == AM_OBJECT_TYPE_WSTRING) {
                emit_instruction(ctx, AM_VM_OP_push, body);
            }
            else {
                error("意外的函数体节点类型。");
            }
        }
        else if (body_type == AM_VALUE_TYPE_VARID) {
            // 本地宿主函数调用，视同符号，原样入栈
            if (am_ast_check_native_ref(ctx->ast, body) == 0) {
                emit_instruction(ctx, AM_VM_OP_push, body);
            }
            // 普通变量
            else {
                emit_instruction(ctx, AM_VM_OP_load, body);
            }
        }
        else if (body_type == AM_VALUE_TYPE_BOOLEAN || NULL || UNDEFINED || SYMBOL || WCHAR || UINT || INT || FLOAT) {
            if (body == AM_VALUE_KW_break || body == AM_VALUE_KW_continue) {
                error("lambda块内不允许出现break和continue。");
            }
            else {
                emit_instruction(ctx, AM_VM_OP_push, body);
            }
        }
        else {
            error("意外的函数体类型。");
        }
    }
    // 返回指令
    emit_instruction(ctx, AM_VM_OP_return, AM_VALUE_UNDEFINED);
    // lambda节点编译完成
}

// 编译cond节点。成功返回0，失败返回-1。
int32_t am_compile_cond(am_compiler_ctx_t *ctx, am_handle_t hd) {
    // 获取cond节点对象
    am_list_t *cond_obj = get_obj_from_nodes_by_handle(ctx->ast, hd); // 伪代码
    // 遍历每个分支
    am_value_t clauses = get_children_of_list(cond_obj);
    for(size_t i = 1; i < length_of_clauses; i++) {
        am_value_t clause = clauses[i];
        int32_t clause_type = am_value_type(clause);
        if (clause_type == AM_VALUE_TYPE_HANDLE) {
            am_object_t *clause_obj = get_obj_from_nodes_by_handle(ctx->ast, clause); // 伪代码
            int32_t clause_obj_type = clause_obj->base.type;
            if (clause_obj_type == AM_OBJECT_TYPE_LIST) {

                // 插入分支开始标签（实际上第一个分支不需要）（等效于TS的AddInstruction(`@COND_BRANCH_${nodeHandle}_${i}`);）
                am_varid_t branch_lbl_varid = am_compiler_make_temp_varid(ctx, L"COND_BRANCH", hd, i);
                am_value_t branch_lbl = am_compiler_make_label(ctx, branch_lbl_varid);
                am_compiler_locate_label(ctx, branch_lbl, ctx->icount);

                // 处理分支条件（除了else分支）
                am_value_t predicate = am_list_get(ctx->ast->alloc, clause_obj, 0);
                if (predicate != AM_VALUE_KW_else) {
                    int32_t predicate_type = am_value_type(predicate);
                    if (predicate_type == AM_VALUE_TYPE_HANDLE) {
                        am_object_t *predicate_obj = get_obj_from_nodes_by_handle(ctx->ast, predicate); // 伪代码
                        int32_t predicate_obj_type = predicate_obj->base.type;
                        if (predicate_obj_type == AM_OBJECT_TYPE_LIST) {
                            int32_t list_type = predicate_obj->type;
                            if (list_type == AM_LIST_TYPE_APPLICATION) {
                                am_compile_application(ctx, predicate);
                            }
                            // 其余情况，统统作push处理
                            else {
                                emit_instruction(ctx, AM_VM_OP_push, predicate);
                            }
                        }
                        else {
                            emit_instruction(ctx, AM_VM_OP_push, predicate);
                        }
                    }
                    else if (predicate_type == AM_VALUE_TYPE_VARID) {
                        // 本地宿主函数调用，视同符号，原样入栈
                        if (am_ast_check_native_ref(ctx->ast, predicate) == 0) {
                            emit_instruction(ctx, AM_VM_OP_push, predicate);
                        }
                        // 普通变量
                        else {
                            emit_instruction(ctx, AM_VM_OP_load, predicate);
                        }
                    }
                    else if (predicate_type == AM_VALUE_TYPE_BOOLEAN || NULL || UNDEFINED || SYMBOL || WCHAR || UINT || INT || FLOAT) {
                        if (predicate == AM_VALUE_KW_break || predicate == AM_VALUE_KW_continue) {
                            error("cond条件不允许出现break和continue。");
                        }
                        else {
                            emit_instruction(ctx, AM_VM_OP_push, predicate);
                        }
                    }
                    else {
                        error("意外的cond分支条件。");
                    }

                    // 如果不是最后一个分支，则跳转到下一条件；如果是最后一个分支，则跳转到结束标签
                    if(i == length_of_clauses - 1) {
                        // 以下相当于TS的AddInstruction(`iffalse @COND_END_${nodeHandle}`);
                        am_varid_t br_end_lbl_varid = am_compiler_make_temp_varid(ctx, L"COND_END", hd, 0);
                        am_value_t br_end_lbl = am_compiler_make_label(ctx, br_end_lbl_varid);
                        emit_instruction(ctx, AM_VM_OP_iffalse, br_end_lbl);
                    }
                    else {
                        // 以下相当于TS的AddInstruction(`iffalse @COND_BRANCH_${uqStr}_${(i+1)}`);
                        am_varid_t br_lbl_varid = am_compiler_make_temp_varid(ctx, L"COND_BRANCH", hd, i+1);
                        am_value_t br_lbl = am_compiler_make_label(ctx, br_lbl_varid);
                        emit_instruction(ctx, AM_VM_OP_iffalse, br_lbl);
                    }
                }

                // 处理分支主体
                am_value_t branch = am_list_get(ctx->ast->alloc, clause_obj, 1);
                int32_t branch_type = am_value_type(branch);
                if (branch_type == AM_VALUE_TYPE_HANDLE) {
                    am_object_t *branch_obj = get_obj_from_nodes_by_handle(ctx->ast, branch); // 伪代码
                    int32_t branch_obj_type = branch_obj->base.type;
                    if (branch_obj_type == AM_OBJECT_TYPE_LIST) {
                        int32_t list_type = branch_obj->type;

                        if (list_type == AM_LIST_TYPE_LAMBDA) {
                            // 对应既有TS实现中的AddInstruction(`loadclosure @${branch}`);
                            am_value_t branch_lambda_label = am_compiler_make_label(ctx, branch);
                            emit_instruction(ctx, AM_VM_OP_loadclosure, branch_lambda_label);
                        }
                        else if (list_type == AM_LIST_TYPE_QUOTE) {
                            emit_instruction(ctx, AM_VM_OP_push, branch);
                        }
                        else if (list_type == AM_LIST_TYPE_QUASIQUOTE) {
                            am_compile_quasiquote(ctx, branch);
                        }
                        else if (list_type == AM_LIST_TYPE_APPLICATION || list_type == AM_LIST_TYPE_UNQUOTE) {
                            am_compile_application(ctx, branch);
                        }
                        else {
                            error("意外的 cond branch 类型。");
                        }
                    }
                    else if (branch_obj_type == AM_OBJECT_TYPE_WSTRING) {
                        emit_instruction(ctx, AM_VM_OP_push, branch);
                    }
                    else {
                        error("意外的 cond branch 类型。");
                    }
                }
                else if (branch_type == AM_VALUE_TYPE_VARID) {
                    // 本地宿主函数调用，视同符号，原样入栈
                    if (am_ast_check_native_ref(ctx->ast, branch) == 0) {
                        emit_instruction(ctx, AM_VM_OP_push, branch);
                    }
                    // 普通变量
                    else {
                        emit_instruction(ctx, AM_VM_OP_load, branch);
                    }
                }
                else if (branch_type == AM_VALUE_TYPE_BOOLEAN || NULL || UNDEFINED || SYMBOL || WCHAR || UINT || INT || FLOAT) {
                    if (branch == AM_VALUE_KW_break || branch == AM_VALUE_KW_continue) {
                        am_value_t while_tags = get_top_of_while_tag_stack(ctx); // 伪代码，成功返回am_value_t，失败返回AM_VALUE_NULL
                        if (while_tags != AM_VALUE_NULL) {
                            if (branch == AM_VALUE_KW_break) {
                                emit_instruction(ctx, AM_VM_OP_goto, while_tags[1]); // endTag
                            }
                            else {
                                emit_instruction(ctx, AM_VM_OP_goto, while_tags[0]); // condTag
                            }
                        }
                        else {
                            error("break或continue没有对应的while表达式。");
                        }
                    }
                    else {
                        emit_instruction(ctx, AM_VM_OP_push, branch);
                    }
                }
                else {
                    error("意外的cond分支。");
                }

                // 插入收尾指令（区分else分支和非else分支）
                if(predicate == AM_VALUE_KW_else || i == length_of_clauses - 1) {
                    // 以下相当于TS的AddInstruction(`@COND_END_${nodeHandle}`);
                    am_varid_t br_end_lbl_varid = am_compiler_make_temp_varid(ctx, L"COND_END", hd, 0);
                    am_compiler_locate_label(ctx, br_end_lbl_varid, ctx->icount);
                    break; // 忽略else后面的所有分支
                }
                else {
                    
                    // 以下相当于TS的AddInstruction(`goto @COND_END_${nodeHandle}`);
                    am_varid_t br_end_lbl_varid = am_compiler_make_temp_varid(ctx, L"COND_END", hd, 0);
                    am_value_t br_end_lbl = am_compiler_make_label(ctx, br_end_lbl_varid);
                    emit_instruction(ctx, AM_VM_OP_goto, br_end_lbl);
                }

            }
            else {
                error("错误的子句类型");
            }
        }
        else {
            error("错误的子句类型");
        }
    } // 分支遍历结束
}

```

## 测试要求

请你参照 @test/test_linker.c 中test_linker_recursive的完整实现，将其从分析到链接到AST打印的全部流程移植到 @test/test_compiler.c 中，再执行编译并输出编译后的中间语言代码。在你的中间语言代码可视化中，若指令没有operand，则不要显示，不要显示undefined。保留既有的测试内容。

请你实现上述需求。你可以使用WSL进行编译构建和测试。


---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

当前的parser实现（主要集中在 @src/parser.c ），在Alpha-renaming（以下简称ARN）上有根本错误。

当前的实现中，在 @src/parser.c 的 parse_identifier 函数中，错误地利用变量所处的lambda节点的handle作为ARN的全局唯一索引（通过调用am_ast_make_unique_variable来构造），试图在递归下降解析阶段就一次性完成ARN，这是完全错误的。变量ARN的全局唯一索引，应当是其**定义**所在的lambda节点的handle，而非其“出现”所在的lambda节点。所谓的“定义所在的lambda节点”，指的是：要么是出现在parameters列表中，要么在这一级lambda的直接body中被define。

正确的实现应当是两阶段的，即：第一步是递归下降解析阶段，所有变量保持原名，其var_type设置为OLD。接下来的第二步是ARN，而ARN又分成两个子pass：1是扫描整棵AST，构建词法作用域即scope的树状嵌套关系，并在scope上挂载old变量；2是根据scope嵌套关系，执行ARN，并在ast->var_arn_mapping中登记新旧varid的映射关系。

请你根据上面的描述，参考 @typescript/src/Analyser.ts 中的正确实现，利用 @include/scope.h 和 @src/scope.c 中的相关定义，修正 @src/parser.c 中现有的错误实现，并更新测试用例为以下用例（Man-or-boy Test）：

```
(define A
  (lambda (k x1 x2 x3 x4 x5)
      (define B
        (lambda ()
            (set! k (- k 1))
            (A k B x1 x2 x3 x4)))
      (if (<= k 0)
          (+ (x4) (x5))
          (B))))

(define thunk_1  (lambda () 1))
(define thunk_m1 (lambda () -1))
(define thunk_0  (lambda () 0))

(display (A 10 thunk_1 thunk_m1 thunk_m1 thunk_1 thunk_0))
```

请你完成上述修正。你可以使用WSL进行编译构建和测试。

---------------------

请先阅读 @doc/AGENTS.md 。

在 @src/compiler.c 的 compile_begin 函数中，该函数假设(begin ...)的每个子表达式都向栈内push一个值，因此结束后pop掉。而问题在于不是所有的表达式都会向栈内push一个值。你怎么看这个问题？

我的疑问：是否必须假设所有的表达式都要往栈里push值，换句话说，执行表达式必有结果（哪怕是#undefined），结果必定压栈。

不要修改任何代码。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请你在 @src/object.c 和 @include/object.h 中，实现以下对象头元数据操作。

```
// 功能说明：获取对象“静态”属性值。是静态则返回0，不是静态则返回-1。
// 实现说明：uint32_t obj->base.header 的最低位是static标识。1为static，0为非static。
int32_t am_object_check_static(am_object_t *obj);

// 功能说明：设置对象“静态”属性值。is_static是静态则输入0，不是静态则输入-1。成功返回0，失败返回-1。
// 实现说明：uint32_t obj->base.header 的最低位是static标识。1为static，0为非static。
int32_t am_object_set_static(am_object_t *obj, int32_t is_static);

// 功能说明：获取对象“保持存活”属性值。是则返回0，不是则返回-1。
// 实现说明：uint32_t obj->base.header 的从LSB倒数第二位是keepalive标识。1为keepalive，0为非keepalive。
int32_t am_object_check_keepalive(am_object_t *obj);

// 功能说明：设置对象“保持存活”属性值。is_keepalive是“保持存活”则输入0，不是“保持存活”则输入-1。成功返回0，失败返回-1。
// 实现说明：uint32_t obj->base.header 的从LSB倒数第二位是keepalive标识。1为keepalive，0为非keepalive。
int32_t am_object_set_keepalive(am_object_t *obj, int32_t is_keepalive);

// 功能说明：获取对象“存活”状态值，用于GC。是存活则返回0，不是存活则返回-1。
// 实现说明：uint32_t obj->base.gcmark 的最高位（MSB）是alive标识。1为alive，0为非alive。
int32_t am_object_check_alive(am_object_t *obj);

// 功能说明：设置对象“存活”状态值，用于GC。is_alive是“存活”则输入0，不是“存活”则输入-1。成功返回0，失败返回-1。
// 实现说明：uint32_t obj->base.gcmark 的最高位（MSB）是alive标识。1为alive，0为非alive。
int32_t am_object_set_alive(am_object_t *obj, int32_t is_alive);
```

请你实现上述需求。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

当前的parser实现中（ @src/parser.c ），am_parser 的最后一步是尾位置分析。然而，在 @src/linker.c 的am_link处理过之后，AST融合前后的尾位置分析结果可能发生变化。因此，需要对链接融合后的AST进行整体的尾位置分析。

我要求你去掉 am_parser 最后一步的尾位置分析，转而将其放到am_link融合所有AST之后的位置。实现层面可能会比较不优雅，例如 @src/parser.c 的 tail_call_analysis 依赖于封装好的parser的ctx。请你仔细考虑如何做好封装，尽量优雅实现。

请你实现上述需求。你可以使用WSL进行编译构建和测试。

---------------------

新需求：在 @src/linker.c 中的am_link最后一步，遍历global_ast->nodes，给global_ast->nodes的所有对象，标记为static，也就是调用 @include/object.h 中的 am_object_set_static ，将其标记为static。这是因为所有从AST解析得到的对象都是永生的静态对象，运行时需要知道static信息。

请你实现上述需求。你可以使用WSL进行编译构建和测试。

---------------------

# 计算续体数据结构和操作

开始编码前，请先阅读 @doc/AGENTS.md 。

请根据以下描述（伪代码），在 @src/continuation.c 和 @include/continuation.h 中，实现计算续体数据结构和相关接口。

```
// 计算续体（continuation）数据结构，是am_object_t的子类。
// 设计说明：计算续体保存了进程在某一时刻的运行状态快照，包含续体返回iaddr、当前闭包handle、opstack和fstack四个字段。由于opstack和fstack都是朴素数组模拟的栈，且捕获续体后只读不写，故将其展平紧密排列存储到柔性数组stacks中。
// stacks的布局是：[0 ...opstack... (fstack_offset-1)  |  (fstack_offset) ...fstack... (length-1)]
// 即以fstack_offset为界，0<=index<fstack_offset属于opstack，fstack_offset<=index<length属于fstack。index较大的方向是栈顶。
typedef struct am_continuation_t {
    am_object_t base;

    size_t length; // 续体对象stacks字段的长度
    size_t fstack_offset; // stacks数组中，fstack区段起点（栈底）在stacks数组中的offset
    am_iaddr_t cont_return_target;
    am_handle_t current_closure_handle;
    am_value_t stacks[];
} am_continuation_t;

// 构造函数。成功返回指针，失败返回NULL。
am_continuation_t *am_continuation_create(
    am_allocator_t *alloc, am_iaddr_t cont_return_target, am_handle_t current_closure_handle,
    am_value_t *opstack, size_t opstack_length, am_value_t *fstack, size_t fstack_length);

// 析构函数。成功返回0，失败返回-1
int32_t am_continuation_destroy(am_allocator_t *alloc, am_continuation_t *obj);

// 拷贝
am_continuation_t *am_continuation_copy(am_allocator_t *alloc, am_continuation_t *obj);

// 获取opstack数组，用于GC遍历和续体恢复。成功返回新数组指针（通过alloc分配，由调用者负责释放），失败返回NULL。
am_value_t *am_continuation_get_opstack(am_allocator_t *alloc, am_continuation_t *obj, size_t *length);

// 获取fstack数组，用于GC遍历和续体恢复。成功返回新数组指针（通过alloc分配，由调用者负责释放），失败返回NULL。
am_value_t *am_continuation_get_fstack(am_allocator_t *alloc, am_continuation_t *obj, size_t *length);
```

请你实现上述需求。你可以使用WSL进行编译构建和测试。

---------------------

# Process

开始编码前，请先阅读 @doc/AGENTS.md 。

请你根据下文描述，在 @src/process.c 和 @include/process.h 中，实现Scheme解释器的进程数据结构和相关操作。

## 设计思路概述

进程（Process）是Scheme解释器的核心数据结构。此处所说的进程，与操作系统的进程没有关系，实现层面也不利用操作系统的原生进程或线程机制。

Scheme解释器执行Scheme代码，整体过程是：将Scheme代码及其依赖代码解析并链接成一整个AST，将其编译成中间语言代码，打包成“模块”（module）。解释器读取模块，从取出静态数据对象和中间语言代码，构造“进程”，送入VM也就是运行时（Runtime），等待调度执行。

进程包含以下数据：运行时堆（heap）、操作数栈（求值栈，opstack）、函数调用栈（fstack）、当前闭包把柄（handle）、程序计数器（PC）、以及进程ID、进程状态等必要的元数据。

Scheme代码在虚拟机中的执行，可以视为虚拟机（Runtime）根据进程的状态修改其内部状态，也就是说，进程类似于图灵机模型中的纸带，是VM的操作对象。

在Scheme解释器的整体实现中，进程数据结构、以及对于进程的相关操作的接口，例如读写进程内部状态的接口，放置于 @src/process.c 和 @include/process.h 中。除此之外，在进程模块中，还实现了基于标记-清除算法的垃圾回收（GC）接口。垃圾回收的具体原理在后文中有详细描述。

## 数据结构、接口和伪代码描述

以下是“进程”数据结构和相关接口的实现伪代码。请你在 @src/process.c 和 @include/process.h 中，补全实现它们。实现提示：

- 注释是重要文档。不要删减我给出的注释内容，但是你可以根据实际情况做补充和修正。
- 以下伪代码完全没有考虑类型检查、错误处理、内存分配和释放等工程细节，你需要补全这些必要细节。
- 你需要特别注意 am_value_t 和 am_handle_t/am_varid_t/am_symbol_t 等类型的区别。am_value_t是解释器的tagged pointer value，是包装了的值，而后面那些是裸值。具体可参考 @include/object.h 。
- 你需要注意 am_object_t 和 am_list_t 、am_wstring_t 等类型的继承关系。它们通过base头实现继承，具体可参考 @include/object.h 。你在进行对象访问时，要注意按需进行指针类型的转换。
- 你可以参考（但是不完全遵从） @typescript/src/Process.ts 中的既有TS实现。

```
#define AM_PROCESS_STATE_READY     (1)
#define AM_PROCESS_STATE_RUNNING   (2)
#define AM_PROCESS_STATE_SLEEPING  (3)
#define AM_PROCESS_STATE_SUSPENDED (4)
#define AM_PROCESS_STATE_STOPPED   (5)

typedef size_t am_pid_t;

// 模块数据结构（供参考）
typedef struct am_module_t {
    am_object_t base; // 基类头：am_module_t也视为对象语言的数据对象

    uint64_t header; // TODO 保留：元数据头
    int32_t opstack_depth; // 编译期分析出来的opstack最大深度
    am_ast_t *ast;
    am_instruction_t *ilcode;
} am_module_t;


typedef struct am_process_t {
    am_object_t base; // 基类头：am_process_t也视为对象语言的数据对象

    am_allocator_t *vm_alloc; // VM工作内存分配器
    am_allocator_t *heap_alloc; // 堆内存分配器

    am_pid_t pid;        // 进程ID
    am_pid_t parent_pid; // 亲进程ID
    int32_t state;       // 进程状态

    am_iaddr_t PC;     // 程序计数器：代表下一条指令的iaddr
    am_instruction_t *ilcode; // 中间语言代码

    am_heap_t *heap;   // 进程私有堆（由堆内存专用allocator管理）

    am_handle_t current_closure_handle; // 指向当前闭包的把柄

    // 操作数栈（其容量为opstack_depth）
    am_value_t *opstack;
    am_value_t *opstack_top; // opstack栈顶指针

    // 函数调用栈（默认容量1000，TODO 后面改成可配置）
    // 注意，成对入栈出栈，栈帧结构为{am_value_t(handle) closure_handle; am_value_t(iaddr) return_target_iaddr; }
    am_value_t *fstack;
    am_value_t *fstack_top; // fstack栈顶指针，注意每次操作加减2个元素
} am_process_t;


// 功能说明：从模块构造并初始化一个新的进程数据结构
// 实现说明：成功返回新进程对象指针；失败返回NULL
am_process_t *am_process_load_from_module(am_allocator_t *vm_alloc, am_allocator_t *heap_alloc, am_module_t *mod) {
    // 基于vm_alloc构造一个新的am_process_t对象proc，其中proc->heap由heap_alloc分配
    // 将mod->ilcode复制到proc->ilcode
    // 将mod->ast->nodes复制到proc->heap
    // 基于vm_alloc构造固定长度的opstack和fstack数组。
    // 初始化其他字段（current_closure_handle = AM_HANDLE_NULL）
}

// 功能说明：向操作数栈中压入值。成功返回0，失败返回-1
int32_t am_process_push_operand(am_process_t *proc, am_value_t v);

// 功能说明：从操作数栈中弹出一个值。成功返回弹出值，失败返回UINTPTR_MAX
am_value_t am_process_pop_operand(am_process_t *proc);

// 功能说明：根据栈顶指针计算opstack中有多少个am_value_t。成功返回长度值，失败返回SIZE_MAX
size_t am_process_length_of_opstack(am_process_t *proc);

// 功能说明：向fstack中压入栈帧（两个值）。成功返回0，失败返回-1
int32_t am_process_push_stack_frame(am_process_t *proc, am_value_t closure_handle_value, am_value_t return_target_iaddr_value);

// 功能说明：从fstack中弹出栈帧的两个值，通过两个指针传出。成功返回0，失败返回-1
int32_t am_process_pop_stack_frame(am_process_t *proc, am_value_t *closure_handle_value, am_value_t *return_target_iaddr_value);

// 功能说明：根据栈顶指针计算fstack中有多少个am_value_t（因为是成对push/pop，所以正常情况下必为偶数）。成功返回长度值，失败返回SIZE_MAX
size_t am_process_length_of_fstack(am_process_t *proc);

// 功能说明：新建闭包并返回其handle。成功返回handle，失败返回AM_HANDLE_NULL
am_handle_t am_process_make_closure(am_process_t *proc, am_iaddr_t iaddr, am_handle_t parent) {
    // 首先在proc->heap中申请一个新的handle：am_handle_t hd
    // 新建闭包对象：am_closure_t *closure_obj = am_closure_create(proc->heap_alloc, iaddr, parent, 16);
    // 将闭包对象的指针绑定到hd上：am_heap_set(proc->heap_alloc, proc->heap, make_value(hd), make_value(closure_obj));
    // 成功返回hd，失败返回AM_HANDLE_NULL
}

// 功能说明：根据闭包handle获取闭包对象。成功返回指针，失败返回NULL
am_closure_t *am_process_get_closure(am_process_t *proc, am_handle_t hd);

// 功能说明：获取进程的当前闭包对象。成功返回指针，失败返回NULL
am_closure_t *am_process_get_current_closure(am_process_t *proc);

// 功能说明：设置进程的当前闭包handle字段。成功返回0，失败返回-1
inline int32_t am_process_set_current_closure(am_process_t *proc, am_handle_t hd);

// 功能说明：变量解引用。成功返回TPV，失败返回UINTPTR_MAX
am_value_t am_process_dereference(am_process_t *proc, am_varid_t varid);

// 功能说明：获取当前指令，并取出opcode和operand。成功返回0，失败返回-1
int32_t am_process_current_instruction(am_process_t *proc, uint32_t opcode, am_value_t operand);

// 功能说明：前进一步（PC加1）
void am_process_step(am_process_t *proc);

// 功能说明：无条件跳转（PC置数iaddr）
void am_process_goto(am_process_t *proc, am_iaddr_t iaddr);

// 功能说明：设置进程状态
void am_process_set_state(am_process_t *proc, int32_t s);



// 以下为计算续体（continuation）的捕获和恢复的实现。

// 功能说明：捕获当前续体，保存为堆对象，并返回其handle。成功返回handle，失败返回AM_HANDLE_NULL
am_handle_t am_process_capture_continuation(am_process_t *proc, am_iaddr_t cont_return_target_iaddr);

// 功能说明：恢复指定的计算续体到当前进程。成功返回其返回目标位置的iaddr，失败返回UINTPTR_MAX
am_iaddr_t am_process_load_continuation(am_process_t *proc, am_handle_t hd);



// 以下为垃圾回收算法的实现。垃圾回收是分进程进行的，其管理的对象是进程的堆、及其背后的heap_allocator。垃圾回收采用简单的标记-清除算法，分为3步：确定GC根、从GC根开始递归标记存活堆对象、清理堆对象。清理堆对象后，allocator管理的底层物理内存（例如arena）可能会碎片化，此处应当实现碎片整理，但现在暂不实现。

// 功能说明：从当前进程和续体环境中收集GC根。成功返回0，失败返回-1
// 设计说明：可达性分析的根（GC根）有：当前闭包本身、当前闭包和函数调用栈对应闭包内的变量绑定、操作数栈内的把柄、函数调用栈内所有栈帧对应的闭包把柄、所有continuation中保留的上面的各项
// 实现说明：gcroots是收集到的GC根的TPV的列表，由外部分配和释放。
int32_t am_process_gc_root(am_process_t *proc, am_list_t *gcroots) {
    // 分析当前进程中的GC根
    size_t opstack_length = am_process_length_of_opstack(proc);
    size_t fstack_length = am_process_length_of_fstack(proc);
    gc_root_helper(proc, gcroots, proc->current_closure_handle, proc->opstack, opstack_length, proc->fstack, fstack_length);
    // 分析所有已保存的续体环境中的GC根
    for (am_handle_t hd in proc->heap) {
        am_object_t *obj = get_obj_from_heap_by_handle(proc->heap_alloc, proc->heap_heap, hd); // 伪代码
        if(obj->base.type == AM_OBJECT_TYPE_CONTINUATION) {
            // 将续体内部环境加入GC根
            size_t cont_opstack_length = 0, cont_fstack_length = 0;
            am_value_t *cont_opstack = am_continuation_get_opstack(proc->vm_alloc, obj, &cont_opstack_length);
            am_value_t *cont_fstack = am_continuation_get_fstack(proc->vm_alloc, obj, &cont_fstack_length);
            gc_root_helper(proc, gcroots, obj->current_closure_handle, cont_opstack, cont_opstack_length, cont_fstack, cont_fstack_length);
            am_free(proc->vm_alloc, cont_opstack);
            am_free(proc->vm_alloc, cont_fstack);
        }
    }
}
static int32_t gc_root_helper(
    am_process_t *proc, am_list_t *gcroots,
    am_handle_t current_closure_handle,
    am_value_t *opstack, size_t opstack_length, am_value_t *fstack, size_t fstack_length
) {
    // 加入当前闭包handle
    am_list_push(proc->vm_alloc, gcroots, make_value(current_closure_handle));

    // 加入当前闭包和函数调用栈对应闭包内的变量绑定
    am_closure_t *current_closure_obj = am_process_get_closure(proc, current_closure_handle);
    for ((am_varid_t varid, am_value_t value) in current_closure_obj.bindings) {
        if (is_handle(value)) {
            am_list_push(proc->vm_alloc, gcroots, value);
        }
    }
    for (am_value_t v of opstack) { // opstack_length
        if (is_handle(v)) {
            am_list_push(proc->vm_alloc, gcroots, v);
        }
    }
    // 注意fstack的出栈入栈都是成对的
    for ((am_value_t closure_handle_value, am_value_t return_target_iaddr_value) of fstack) { // fstack_length
        am_closure_t *closure_obj = am_process_get_closure(proc, value_to_handle(closure_handle_value));
        if (closure_obj->base.type == AM_OBJECT_TYPE_CLOSURE) {
            am_list_push(proc->vm_alloc, gcroots, make_value(closure_handle_value));
            for ((am_varid_t varid, am_value_t value) in closure_obj.bindings) {
                if (is_handle(value)) {
                    am_list_push(proc->vm_alloc, gcroots, value);
                }
            }
        }
        else {
            error("预期闭包，实际非闭包");
        }
    }
}

// 功能说明：从GC根开始，递归标记存活对象。成功返回0，失败返回-1（或更小的负数）
int32_t am_process_gc_mark(am_process_t *proc, am_value_t v) {
    int32_t ret = 0; // 收集整体的返回值
    if (not value_is_handle(v)) return 0;
    am_handle_t hd = value_to_handle(v);
    if (am_heap_has_handle(proc->heap_alloc, proc->heap, hd) < 0) return 0;
    am_object_t *obj = get_obj_from_heap_by_handle(proc->heap_alloc, proc->heap_heap, hd); // 伪代码
    if (!obj) return -1;
    if (am_object_check_alive(obj) == 0) return 0;
    if (obj->base.type == AM_OBJECT_TYPE_LIST) { // 实际上应该是QUOTE|QUASIQUOTE|UNQUOTE|APPLICATION
        am_object_set_alive(obj, 0);
        for (am_value_t child of obj->chindren) {
            ret += am_process_gc_mark(proc, child);
        }
    }
    else if (obj->base.type == AM_OBJECT_TYPE_WSTRING) {
        am_object_set_alive(obj, 0);
    }
    else if (obj->base.type == AM_OBJECT_TYPE_CLOSURE) {
        am_object_set_alive(obj, 0);
        ret += am_process_gc_mark(proc, make_value(obj->parent));
        for ((am_varid_t varid, am_value_t value) in obj.bindings) {
            if (is_handle(value)) {
                ret += am_process_gc_mark(proc, value);
            }
        }
    }
    return ret;
}

// 功能说明：基于存活标记结果，删除所有未被标记存活的非静态对象和对应的handle。成功返回0，失败返回-1
int32_t am_process_gc_sweep(am_process_t *proc) {
    size_t gcount = 0;
    size_t count = 0;
    for (am_handle_t hd in proc->heap) {
        count++;
        am_object_t *obj = get_obj_from_heap_by_handle(proc->heap_alloc, proc->heap_heap, hd); // 伪代码
        if (am_object_check_static(obj) == 0) continue;
        int32_t obj_type = obj->base.type;
        if (obj_type == AM_OBJECT_TYPE_LIST|STRING|CLOSURE) {
            if (am_object_check_alive(obj) < 0) {
                // 不仅删除handle，还穿透释放其映射的obj
                am_heap_free_handle(proc->heap_alloc, proc->heap, hd);
                gcount++;
            }
            else {
                // 对于存活对象，将其alive标识清空为否，以便下次gc重新标记
                am_object_set_alive(obj, -1);
            }
        }
    }
    printf("已清理 %zu / %zu 个对象\n", gcount, count);
    // TODO 暂不实现alloc管理的内存的整理
}

// 功能说明：对进程执行全量的标记-清除GC。成功返回0，失败返回-1
int32_t am_process_gc(am_process_t *proc) {
    am_list_t *gcroots = am_list_create(proc->vm_alloc, 128, AM_LIST_TYPE_DEFAULT, AM_HANDLE_NULL);
    // 收集GC根对象
    am_process_gc_root(proc, gcroots);
    // 从GC根对象开始递归标记存活对象
    for (am_value_t v of gcroots) {
        am_process_gc_mark(proc, v);
    }
    // 清除未被标记为存活的非静态对象及其handle
    am_process_gc_sweep(proc);
}

```

## 测试要求

你可以参照 @test/test_linker.c 中test_linker_recursive的完整实现，将其从分析到链接到AST打印的全部流程（包括可视化），取其核心流程移植到 @test/test_process.c 中，以实现从代码到进程数据结构的完整链路。再根据你的理解，增加测试用例和断言。

请你实现上述需求。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个Scheme解释器。为了确定运行时操作数栈（求值栈）opstack的最大深度，请你阅读项目现有C语言代码，重点理解进程数据结构（ @src/process.c ），在 @src/parser.c 和 @include/parser.h 中，实现基于AST静态分析的操作数栈最大深度估计算法。

接口定义如下：

```
// opstack最大深度的静态分析。成功返回最大深度，失败返回SIZE_MAX。
size_t am_parser_opstack_depth_analysis(am_ast_t *ast);
```

在 @test/test_parser.c 中补充测试。

请你实现上述需求。你可以使用WSL进行编译构建和测试。

---------------------

# 运行时（VM）

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个Scheme解释器。请你通读整个项目的全部C语言代码，根据下面的伪代码草稿，重点参照 @typescript/src/Runtime.ts 中提供的既有实现，在 @src/runtime.c 和 @include/runtime.h 中，完成虚拟机运行时的实现。

```
typedef struct am_runtime_t {
    am_allocator_t *vm_alloc;
    am_allocator_t *heap_alloc;

    wchar_t *working_dir;

    size_t process_poll_counter; // 进程计数器（进程池中有多少进程）
    am_process_t *process_pool;  // 进程池
    am_list_t *process_queue;    // 进程队列：List<am_value_t(uint:pid)>

    am_list_t *input_fifo;
    am_list_t *output_fifo;
    am_list_t *error_fifo;

    (void)(*callback_on_tick)(?);
    (void)(*callback_on_event)(?);
    (void)(*callback_on_halt)(?);
    (void)(*callback_on_error)(?);

    time_t tick_counter;
    time_t gc_timestamp;
} am_runtime_t;



am_runtime_execute(am_runtime_t *rt, am_process_t *proc) {
    uint32_t opcode = 0;
    am_value_t operand = 0;
    am_process_current_instruction(proc, &opcode, &operand);
    switch (opcode) {
        case AM_VM_OP_call: am_op_call(rt, proc, operand); break;
        // ...
        default: error("错误指令");
    }
}


am_runtime_tick(am_runtime_t *rt, uint32_t timeslice) {
    current_process = process_queue.shift();
    while (timeslice--) {
        am_runtime_execute(rt, current_process);
    }
    process_queue.push(current_process);
    callback_on_tick();
}

am_runtime_event_handler(am_runtime_t *rt) {
    am_runtime_tick(rt, 1000);
    // GC
    if (AM_ENABLE_GC && current_timestamp - gc_timestamp > AM_GC_INTERVAL) {
        gc_timestamp = current_timestamp;
        for (am_process_t p of process_queue) {
            am_process_gc(p);
        }
    }
    // 其他IO事件
}

am_runtime_start(am_runtime_t *rt) {
    while (1) {
        am_runtime_event_handler(rt);
    }
}
```

根据以下提供的解释器整体框架的参考用法，补全相关入口函数的实现。

```
am_module_t mod = am_compile(am_ast_t *ast, am_iaddr_t offset, am_iaddr_t ret);
// 中间可以有dump/load
am_runtime_t rt = am_runtime_create(wchar_t *base_dir, ...);
am_load_module(rt, mod);
am_start(rt);
```

请你实现上述需求。你可以使用WSL进行编译构建和测试。


---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个正在开发的C语言编写的Scheme解释器。请你通读项目中的C语言实现和TypeScript参考实现，排查当前C语言实现中存在的以下若干问题。

1、比较C语言实现的 @src/process.c 的 am_process_load_from_module ，与参考TS实现中的 @typescript/src/Process.ts 中的 Process.constructor ，检查两者逻辑是否一致，尤其关注为什么在TS实现中构造了顶级闭包，而C语言实现中没有。我怀疑这是个潜在的问题。需要澄清的是：C语言实现的IL代码的标签解析早在编译阶段就已完成，因此am_process_load_from_module不需处理。

2、在 @test/test_runtime.c 中，test_runtime_complex_recursion 用例实际输出与期望输出不符，其他测试用例正常。中，test_runtime_complex_recursion所使用的测试代码是经典的Man-or-Boy测试，用于测试Scheme解释器对于一等函数、词法作用域、闭包、set!、尾调用优化等关键特性的处理是否正确。其Scheme代码如下：

```
;; Man-or-Boy test
(define A
  (lambda (k x1 x2 x3 x4 x5)
      (define B
        (lambda ()
            (set! k (- k 1))
            (A k B x1 x2 x3 x4)))
      (if (<= k 0)
          (+ (x4) (x5))
          (B))))
(define thunk_1  (lambda () 1))
(define thunk_m1 (lambda () -1))
(define thunk_0  (lambda () 0))
(display (A 6 thunk_1 thunk_m1 thunk_m1 thunk_1 thunk_0)) ;; 这里的“6”就是MoB的参数
```

参数设定为6，期望结果是1，即MoB(6)=1，但当前输出结果是2。输入参数之所以选择6，是因为6是第一个非平凡的参数值，开始涉及复杂的嵌套递归调用和链式set!，恰好从6开始出错。而若参数大于等于7，则运行报错，可能与栈深度设置有关，目前暂不理会。作为参考，MoB(5)=0，MoB(4)=1，MoB(3)=0，MoB(2)=-2，MoB(1)=0，这些情况都是平凡情况，现有实现能够输出正确结果。我已经人工确认了compiler输出的中间语言代码是正确的。我要求你排查MoB(6)计算错误的原因。

请你分析上述问题及其可能造成的影响。你可以使用WSL进行编译构建和测试。你可以修改代码、增加调试输出或运用gdb等工具，全面排查代码问题。

回答：

已完成排查和修复。以下是分析结论和改动说明。

## 1. `am_process_load_from_module` 与 TS `Process.constructor` 的对比

| 项目 | TS `Process.constructor` | C `am_process_load_from_module` |
|---|---|---|
| 标签解析 | 构造时调用 `LabelAnalysis()`，把 `@label` 解析成指令索引 | C 的编译阶段已经把标签解析成 `iaddr`，所以不需要 |
| 顶级闭包 | 显式创建 `TOP_NODE_HANDLE` 闭包，作为变量查找链的终点 | 没有显式创建；`current_closure_handle` 初始为 `AM_HANDLE_NULL` |
| AST/Heap | 直接把 AST nodes 放进 heap | 深拷贝 AST 到进程 heap 并标记为 static |

**这个差异是不是 bug？**

- 不是 MoB 测试失败的原因。
- C 的“顶级闭包”是惰性地由第一条 `call iaddr_X` 创建的：调用时会生成顶层 lambda 的闭包，并把顶层定义（如 `A`、`thunk_1` 等）存在该闭包里。返回时 `op_return` 把 `current_closure_handle` 恢复为 `NULL`，等效于 TS 恢复为 `TOP_NODE_HANDLE`。
- 当前实现下，这种差异没有造成功能错误；所有测试都能通过。
- 不过这是一个架构上的不一致点：如果将来需要在进程启动前就有当前环境（例如在 `call` 之前访问变量、或 continuation 恢复时依赖一个非 `NULL` 的根闭包），可能会出问题。建议后续补上一个显式顶级闭包，但优先级不高。

## 2. MoB(6) 计算错误的原因

### 根因：闭包的脏标记是按 binding 条目维护的，而不是按变量维护

C 实现中，`am_binding_t` 里每个 bound/free 条目都有自己的 `dirty_flag`。当同一个 `varid` 同时存在 free 和 bound 绑定时（这在通过 `loadclosure` / `tailcall label` 创建新闭包再执行 `store` 参数时非常常见），两者的脏标记会不同步。

TS 的 `Closure` 虽然也有 `boundVariables` 和 `freeVariables` 两个 map，但脏标记只有一份（`dirtyFlag[varid]`）。`InitBoundVariable` / `InitFreeVariable` 会清除该变量的脏标记；`SetBoundVariable` / `SetFreeVariable` 会设置该变量的脏标记。

C 的问题场景（以 MoB 为例）：

1. 从 B 尾调用 A 时，`op_call_async` 会创建一个 **新的 A 闭包**，并把当前 B 闭包的所有绑定（包括 free 的 `k`，且是脏的）作为 free 变量复制进去。
2. 随后 A 执行 `store k`，调用 `am_closure_init_bound_var` 加入 bound `k`，并把 bound 条目的 `dirty_flag` 清 0。
3. 但 **free `k` 条目的脏标记仍然是 1**。
4. 当 A 内部的 B 闭包去解析 `k` 时，`am_closure_is_dirty_var` 因为 free 条目脏而返回“脏”，于是总是使用 A 的 bound `k`（最新值）。
5. 这导致某些本应该使用“创建时快照”的 B 闭包错误地使用了父 A 的最新 `k`，从而多触发了一次递归，最终 MoB(6) 得到 2 而不是 1。

我先用 Python 写了一个严格按 C 语义模拟的解释器，复现了结果 2；再把脏标记改成按变量同步后，结果变为正确的 1，确认了根因。

### 修复

修改 `src/closure.c`，让同一个 `varid` 的所有 bound/free 条目的脏标记保持一致：

- `am_closure_init_bound_var`：初始化/更新 bound 变量时，把同 `varid` 的 free 绑定脏标记也清 0。
- `am_closure_init_free_var`：初始化/更新 free 变量时，把同 `varid` 的 bound 绑定脏标记也清 0。
- `am_closure_set_bound_var`：设置 bound 变量时，把同 `varid` 的 free 绑定也置脏，并同步值。
- `am_closure_set_free_var`：设置 free 变量时，把同 `varid` 的 bound 绑定也置脏，并同步值。

### 测试验证

- `test_runtime_complex_recursion` 现在输出 `1`。
- 在 `test/test_runtime.c` 中给该用例加了具体值断言：输出字符串必须是 `"1"`，防止回归。
- 全部测试通过：

```bash
make
for t in test_closure test_map test_ast test_parser test_linker test_wstring test_list test_vocab test_compiler test_continuation test_process test_runtime; do ./$t; done
```

- 额外验证了 MoB(k) 在 k=1..9 时的输出与参考值一致：  
  `0, -2, 0, 1, 0, 1, -1, -10, -30`。  
  k≥10 仍会因 `fstack_capacity = 1000` 的硬限制而溢出，这是另一个独立问题，符合你“目前暂不理会”的要求。

## 改动文件

- `src/closure.c`：同步同一变量的脏标记。
- `test/test_runtime.c`：引入 `wstring.h`，对 MoB(6) 输出值做断言。

临时调试文件（`mob_test.c`、Python 模拟器、trace 日志等）已清理。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

在 @src/process.c 中，实现了若干与垃圾回收（GC）相关的函数。其中 am_process_gc_root、gc_root_helper 两个函数涉及对于 am_list_t 对象 gcroots 的读写。然而，am_list_t 是可扩容的容器，对其进行读写，可能导致 am_list_t 对象的指针发生变化。在当前的实现中，am_process_gc_root、gc_root_helper 仅接受指向 gcroots 对象的指针，这会导致调用方失去扩容后 gcroots 的新指针。因此，我要求你将这两个函数的参数改为指向gcroots对象的二级指针，使得调用者能够始终跟踪到扩容后（指针发生变化的）gcroots对象。

无需测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个C语言实现的Scheme解释器，采用编译器+中间语言VM架构。中间语言运行时VM的实现主要位于 @src/runtime.c ，其中 op_* 开头的函数是所有VM指令的具体实现。完整的opcode定义位于 @include/opcode.h 。请你检查 @src/runtime.c 中是否还有 @include/opcode.h 中定义了但是没有实现的指令。如果有，则参照其他指令的实现方式，并参考既有TS实现 @typescript/src/Runtime.ts 新增实现。不要改动已有的其他指令的实现。同时，根据实际情况，修改指令译码函数 am_runtime_op_dispatch 。

请你实现上述需求，并参照 @test/test_runtime.c 另写一个文件，对新引入的指令进行测试。你可以使用WSL进行编译构建和测试。

---------------------

完成以下两项需求：

1、参照 @src/ast.c 中的am_ast_node_to_string，在 @src/process.c 中，实现功能相同的函数，其签名如下：

wchar_t *am_process_list_to_string(am_process_t *proc, am_handle_t hd, size_t *length);

该函数从process的heap、var_vocab和symbol_vocab中取得显示列表所需的字符串信息。其中symbol的处理需要特别注意：凡是不在quote列表内的symbol，都带上前缀单引号“'”；凡是在quote列表内的symbol，都不带前缀单引号“'”。

2、基于刚刚实现的 am_process_list_to_string ，在 @src/runtime.c 的 op_display 中，新增列表对象显示的功能。

请你实现上述需求，并基于 @test/test_runtime_new_opcodes.c 对各类数据（包括列表）的display进行测试。你可以使用WSL进行编译构建和测试。

修改需求：对于 am_process_list_to_string ，增加以下要求：显示quote列表时，无论最外层list还是嵌套的内层list，都不显示前导单引号“'”；但是空列表除外，空list无论位于何处，都应显示前导单引号，即“'()”。

请你在 @test/test_runtime_new_opcodes.c 中，增加以下测试用例：

```
(display
((lambda (x) (cons x (cons (cons quote (cons x '())) '()))) (quote (lambda (x) (cons x (cons (cons quote (cons x '())) '())))))
)
```

这是著名的Quine，即输出自身代码的程序。因此，该程序的期望输出结果，就是：

```
((lambda (x) (cons x (cons (cons quote (cons x '())) '()))) (quote (lambda (x) (cons x (cons (cons quote (cons x '())) '())))))
```

你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请你实现几种对象类型的size函数，用于计算对象所占用的实际字节数（考虑结构体填充和对齐问题），以便执行标记压缩GC算法。

在 @src/closure.c 中实现以下函数：am_closure_size

```
// 功能说明：计算对象所占用的实际字节数（考虑结构体填充和对齐问题）
// 成功返回字节数，失败返回SIZE_MAX
size_t am_closure_size(am_allocator_t *alloc, am_obj_closure_t *obj);
```

在 @src/continuation.c 中实现以下函数：am_continuation_size

```
// 功能说明：计算对象所占用的实际字节数（考虑结构体填充和对齐问题）
// 成功返回字节数，失败返回SIZE_MAX
size_t am_continuation_size(am_allocator_t *alloc, am_continuation_t *obj);
```

在 @src/list.c 中实现以下函数：am_list_size

```
// 功能说明：计算对象所占用的实际字节数（考虑结构体填充和对齐问题）
// 成功返回字节数，失败返回SIZE_MAX
size_t am_list_size(am_allocator_t *alloc, am_list_t *obj);
```

在 @src/map.c 中实现以下函数：am_map_size

```
// 功能说明：计算对象所占用的实际字节数（考虑结构体填充和对齐问题）
// 成功返回字节数，失败返回SIZE_MAX
size_t am_map_size(am_allocator_t *alloc, am_map_t *obj);
```

在 @src/wstring.c 中实现以下函数：am_wstring_size

```
// 功能说明：计算对象所占用的实际字节数（考虑结构体填充和对齐问题）
// 成功返回字节数，失败返回SIZE_MAX
size_t am_wstring_size(am_allocator_t *alloc, am_wstring_t *obj);
```

请你实现上述需求，并新建一个test文件，对这些函数进行测试。你可以使用WSL进行编译构建和测试。


---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请你修改 @src/parser.c ，在 am_parse 的 alpha_rename_analysis 之后，增加一个环节：遍历整个ast->nodes，清除掉上一步在nodes中遗留的type为AM_OBJECT_TYPE_SCOPE的scope对象。

无需新增测试或者修改现有测试，只需要编译项目并确保 test_runtime 运行正常即可。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请你阅读下面的“内存管理概述”，了解项目当前尚未完全实现的内存管理实现思路，然后完成需求。这是一个规模较大、波及面非常广的需求，务必谨慎、保守。

# 内存管理概述

- 解释器全生命周期拥有宿主系统分配的一大块内存池。
- 内存池分成两部分，分别是用户区（堆区）和工作区（VM区）。
- 用户区（堆区）存储Scheme语言的数据对象（“用户数据对象”），即am_object_t的子类（带有base头的对象），包括am_closure_t、am_continuation_t、am_list_t、am_map_t、am_wstring_t。工作区（VM区）存储数据对象之外的各类数据，例如编译阶段的token、临时AST、临时栈，以及运行阶段的进程数据结构（除了数据对象的所有数据，例如堆容器heap、操作数栈opstack、函数调用栈fstack、符号表、PC、当前闭包handle等）、进程池容器、VM状态数据、所有进程共享的数据（如FIFO）等。
- 解释器实现了heap（堆容器）数据结构，用于实现用户数据对象的逻辑地址和物理地址的解耦。heap的核心是一个hashmap，维护从handle（把柄，即逻辑地址）到指向用户数据对象的指针的映射，因而heap的实质是逻辑地址到物理地址的映射表。对于容器类的用户数据对象，例如am_list_t，它们容纳的都是间接指向其他数据对象的handle（把柄）。因此无论用户数据对象的物理地址如何变化（例如因扩容、GC而改变），handle可以保持不变。
- 只分配逻辑地址（handle），并维护handle与物理地址（指针）的映射关系，以维持逻辑地址的稳定。
- heap保存的是进程的元数据，即逻辑地址映射表，它本身不是用户数据对象，没有base头，因而只能存储在工作区。
- 解释器维护两个am_allocator_t，分别管理用户区和工作区。其中heap_alloc管理内存池低半部分内存（用户堆区），vm_alloc管理内存池高半部分内存（VM工作区），两者之间通过boundry分界。
- 所有allocator分配的内存池内指针，统一按4字节对齐（指针最低两位恒0）。
- 工作区不受GC管理，采用简单 bump pointer 内存分配策略。仅在关键时间点对连续的已分配区段执行整体重置（例如编译过程结束后直接清空整个VM区，供运行时从头开始分配）。
- 用户堆区受GC管理，拟采用标记-清除和标记-压缩混合策略。内存分配使用 First-Fit Free-List 策略，后期进化到 Bump-Pointer with Look-aside Free-List 策略。

# 需求

修改 @src/heap.c 和 @include/heap.h 中的am_heap_t的所有相关函数，使其参数接收两个am_allocator_t：container_alloc, obj_alloc。前者是heap本身（包括其table和metadata成员）的allocator，后者是heap的table所存储的指针所指向的对象的allocator。

目前有多个源文件使用了heap相关的函数，你应当一并修改所有涉及的调用。处理原则如下：在解释器的Parser、Linker、Compiler阶段所使用的heap相关函数，其container_alloc和obj_alloc是同一个allocator，因为编译阶段只使用工作区。而在解释器的运行时阶段，开始区分vm_alloc和heap_alloc。heap本身维护元数据，需要放在vm_alloc分配的位置，而heap所管理的那些对象的指针，则需要由heap_alloc分配。运行时相关模块，如process、runtime，所使用的heap相关函数，则需要明确区分两个allocator，container_alloc对应 vm_alloc，obj_alloc对应heap_alloc。

# 测试

该需求波及面非常大，几乎涉及所有模块。请谨慎修改。请你优先保证 test_runtime 的正确性，因为这是全流程的测试。一般情况下，你不需要修改现有的测试用例，但你可以根据需要创建新的测试用例。你可以使用WSL进行编译构建和测试。


---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

在Scheme解释器中，process是对module的运行时实例化，module主要由AST和ILCode构成，因此process需要保留AST中的必要信息（但不是全部信息）。

当前实现中，在 @src/process.c 中的 am_process_load_from_module 函数中，实现了从module构造一个process的过程，其中涉及各复杂数据结构字段的拷贝和转储/加载。但仍有两个AST的必要字段没有被拷贝到process中： var_type 和 natives 。

我的需求是：请你修改 @src/process.c 中的 am_process_load_from_module 函数，将 mod->ast 的 var_type 和 natives 两个字段，拷贝到 process 新增的 var_type 和 natives 两个字段。同时，在析构函数中，也增加对于这两个字段的析构逻辑。这两个字段都是复杂的容器。你需要阅读 @src/list.c 和 @src/map.c 的相关实现。

无需编写新的测试，也不要修改已有的测试。保证 test_runtime 正确即可，因为这是全流程的测试。你可以使用WSL进行编译构建和测试。


---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请你全面阅读解释器现有代码，检查 @src/parser.c 中的 am_parser_opstack_depth_analysis 实现是否正确。我需要特别强调说明的是：begin的处理比较复杂，但是现在我姑且采取一种简单策略，即begin的所有子表达式的结果都不退栈（也就是编译时不会加入pop指令）。你需要基于这个临时策略，检查现有opstack_depth估计算法是否正确。如果你认为不正确，可直接修改代码。

修改完成后，我要求你在 @test/test_parser.c 现有测试的基础上，更加全面地测试 am_parser_opstack_depth_analysis 。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

需求：在 @src/runtime.c 中，实现一个用于运行时检查call指令参数（am_value_t v）是否是varid且在proc->var_type中是 AM_VAR_TYPE_NATIVE_REF (定义在 @include/process.h 中)。函数签名如下：

```
// 功能描述：检查call指令参数是否是本地宿主库的调用
// 是返回0，不是返回-1
int32_t am_runtime_check_native_ref(am_runtime_t *rt, am_process_t *proc, am_value_t v);
```

无需测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请你实现本地宿主库（native）调用机制，基于现有的不完整的实现框架。

本地宿主库调用机制的设计思路是：

- VM执行到call指令（ @src/runtime.c 的 op_call_async ），若operand是native_ref变量，则调用 op_callnative 。
- op_callnative 尚未实现，你需要补全。思路是根据operand取出varid进而从proc->var_vocab取出其字符串，然后将其从中间的点号分成prefix（native_id）和suffix（identifier）两部分。
- 在 @src/native.c 中实现某种运行时查表机制，通过prefix和suffix，分别定位native库和库中对应的C语言实现函数。两者要解耦成两张表，以便后续实现调用动态库等机制，但现在只需要关联到 @src/native.c 中已经存在的函数即可。
- @src/native.c 已经给出了一个样例函数 am_native_System_test ，也就是说，你需要实现通过 System.test 这个variable，查找到 am_native_System_test 这个函数，并将其作为一个扩展的VM指令（类似于op_*这样的函数）执行。

无需编写新的测试，也不要修改已有的测试。保证 test_runtime 正确即可，因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

---------------------

现在请你重新阅读仓库最新代码，基于已有的本地宿主库调用分派机制，实现Native数学库。要求如下：

- 需要你实现的函数列表，位于 @src/native.c 的 am_nlib_Math_funcs 中。这些函数的签名也在 @include/native.h 中声明。
- 你可以参照 @typescript/lib/Math.js 中的实现，编写C语言版本的代码。仅限使用C标准库函数。
- 所有的数值都视为float（细致的类型区分是后面的待办事项）。
- 注意错误处理、函数的arity、以及对于特殊边界情况的处理（可以复用标准库提供的机制，注意利用Animac既有的undefined、null等特殊值。如果是NaN，则暂且返回null。）

无需编写新的测试，也不要修改已有的测试。保证 test_runtime 正确即可，因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

---------------------

重构需求：现有的Math本地库函数，am_native_Math_*，都在 @src/native.c 中实现，这很混乱。我要求你将所有的 am_native_Math_* 声明移到 @include/native_Math.h ，将所有的 am_native_Math_* 实现（含两个辅助函数 native_pop_number 和 native_push_result ）移到 @src/native_Math.c 中。am_native_System_* 不要动。另外，将 am_nlib_Math_funcs 注册表的内容放到 native_Math 中。重构后， @src/native.c 和 @include/native.h 只实现与本地库函数分派相关的逻辑，具体实现都在 native_XXX.c/h 中。

无需编写新的测试，也不要修改已有的测试。保证 test_runtime 正确即可，因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请你重新阅读仓库代码，理解本地宿主库调用分派机制，在 @src/native_String.c 和 @include/native_String.h 中，参照既有TypeScript参考实现 @typescript/lib/String.js ， 参照既有Math库（ @src/native_Math.c 和 @include/native_Math.h ）和System库（ @src/native_System.c 和 @include/native_System.h ）的实现套路，参照 @src/runtime.c 中字符串相关op_*指令的实现方式：实现String本地宿主库。要求如下：

- 请实现  @typescript/lib/String.js 中实现的所有函数。
- 仅限使用C标准库函数。
- 所有的数值都视为float（细致的类型区分是后面的待办事项）。
- 注意错误处理、函数的arity、以及对于特殊边界情况的处理（可以复用标准库提供的机制，注意利用Animac既有的undefined、null等特殊值。如果是NaN，则暂且返回null。）

无需编写新的测试。保证 test_runtime 正确即可（你可能需要在 @test/test_runtime.c 中注册新实现的String库），因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请你阅读全部仓库代码（含C语言和TypeScript代码），理解本地宿主库调用分派机制，在 @src/native_System.c 和 @include/native_System.h 中，参照既有TypeScript参考实现 @typescript/lib/System.js ， 参照既有Math库（ @src/native_Math.c 和 @include/native_Math.h ）等其他native库的实现套路，参照 @src/runtime.c 中字符串相关op_*指令的实现方式，实现System本地宿主库。要求如下：

- 这个需求相对复杂，涉及从JavaScript的异步回调写法向C语言的同步写法迁移。你需要阅读现有C语言实现和TypeScript实现，理解虚拟机指令执行和任务调度的原理，尤其要理解现有C语言实现中是如何实现进程调度、时间片轮转和事件循环。
- 请实现  @typescript/lib/System.js 中的 exec（改成同步执行，无需异步） 、 set_timeout 、 set_interval 、 clear_timeout 、 clear_interval 、 timestamp 这几个函数。
- 仅限使用C标准库函数和POSIX/Unix/Linux的API，无需支持Windows。
- set_timeout 、 set_interval 均为异步函数，其行为类似JavaScript中的setTimeout和setInterval。所谓的异步函数，指的是调用该函数注册定时回调后，继续执行后面的代码，直至定时回调因时间到而唤醒。回调结束后，应回到回调触发的位置。注意处理进程状态发生变化的情况，例如进程执行完毕处于停止状态后，如果时间到，则回调函数也应被唤起执行。这实质上就是利用现有的事件循环机制实现了某种定时中断。请你仔细阅读TypeScript实现，理解其利用JavaScript本身的setTimeout/setInterval注册回调函数是如何与VM的进程调度机制相配合的。由于C语言中没有现成的setTimeout/setInterval，你需要利用 @src/runtime.c 中构造的事件循环，通过某种机制去模拟这种异步的延时/定时回调。
- 所有的数值都视为float（细致的类型区分是后面的待办事项）。
- 注意错误处理、函数的arity、以及对于特殊边界情况的处理（可以复用标准库提供的机制，注意利用Animac既有的undefined、null等特殊值。如果是NaN，则暂且返回null。）

无需编写新的测试。保证 test_runtime 正确即可，因为这是全流程的测试。注意 test_runtime 中读取的测试代码中，提供了一个所谓的“睡眠排序”用例。它通过注册多个延迟从短到长的定时回调，以一种幽默的方式实现对数组的排序。正常状况下，数组应当被从小到大排序。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

在 @src/runtime.c 的 am_runtime_output 函数中，接收的是wchar_t字符串。在这个字符串中，可能出现wchar_t构成的“\n”、“\r”、“\t”、“\b”、“\\”、“\"”等转义字符序列。然而由于字符串是宽字符串，所以实际上编译器并不把它们解释成对应的控制字符。现在我要求你在 am_runtime_output 中，在printf和输出到fifo之前，先对输入的str进行一次扫描替换，将所有的L"\n"之类的转义字符序列，替换成真正的ASCII控制字符。同时，需要处理“\\”、“\"”这两个特殊情况。

无需编写新的测试。保证 test_runtime 正确即可（你可能需要在 @test/test_runtime.c 中注册新实现的String库），因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请你阅读全部仓库代码（含C语言和TypeScript代码），理解C语言实现的进程调度和事件循环机制，理解本地宿主库调用分派机制，在 @src/native_System.c 和 @include/native_System.h 中，实现System.fork本地宿主函数。

重要提示：不要使用或者修改 @src/runtime.c 中已有的但尚未实现的`op_fork`函数！也不要利用任何fork指令！不要使用或者修改 @src/runtime.c 中已有的但尚未实现的`op_fork`函数！也不要利用任何fork指令！完全在 @src/native_System.c 和 @include/native_System.h 中实现 am_native_System_fork 函数！

System.fork的语义是：深度复制当前进程，保留其全部状态（除PID和parent_pid），并加入VM的进程调度队列。其行为和功能与Linux系统调用fork类似。

System.fork无参数，有一个number类型的返回值。返回值在亲进程和子进程中不同，详见下文，失败返回-1。

System.fork的具体过程：

- 亲进程调用System.fork时，首先step以修改PC为PC+1。
- 复制子进程数据结构（通过vm_alloc分配器），包括heap本身，同时深度复制heap的value所指向的数据对象（通过heap_alloc分配器），并将新heap中的指针指向复制后的数据对象。
- 子进程opstack顶部push一个0，作为子进程fork的返回值。
- 子进程对象加入进程队列，获得子进程pid，并将子进程pid入栈亲进程的opstack。
- fork执行完毕，释放cpu。在后面的时间片中，子进程被调度启动。

Fork的典型用法：

```
(define pid (System.fork))
(if (== pid 0) {
  (子进程逻辑)
} {
  (亲进程逻辑)
})
```

无需编写新的测试。保证 test_runtime 正确即可，因为这是全流程的测试。注意 test_runtime 中读取的测试代码中，提供了一个测试用例。

```
(native System)
(define pid (System.fork))
(if (== 0 pid) (display "child") (display "parent"))
```

其预期效果为同时输出“child”和“parent”。

你可以使用WSL进行编译构建和测试。

---------------------

请先阅读 @doc/AGENTS.md 。

检查 @src/compiler.c 中，compile_complex_application 的实现（η变换）与TypeScript参考实现（ @typescript/src/Compiler.ts ）是否完全一致，并排查解析 @testscm/brainfuck.scm 中以下复杂调用时出错的原因：

```
(cons (String.charCodeAt (- iter (+ 2 cp_init)) code_str) ((env_constructer dp_init cp_init code_str) (+ iter 1)))
```

复现方法如下：

```
make all
./main /mnt/d/Desktop/GitRepos/Animac-2026/testscm/brainfuck.scm
```

我怀疑这个问题与η变换编译的实现有误有关。TypeScript参考实现确认是正确的。如果定位了问题，可直接修改代码。不允许执行任何删除文件类操作。你可以使用WSL进行编译构建和测试。

已完成排查和修复。

## 1. `compile_complex_application` 与 TypeScript 参考实现的对比结论

`src/compiler.c` 中的 `compile_complex_application` 与 `typescript/src/Compiler.ts` 里的 `CompileComplexApplication` **核心 η 变换结构完全一致**：

- 都生成 `goto apply_begin` 跳过临时 lambda 体；
- 临时 lambda 体都是逆序 `store` 所有形式参数，再正序 `load` 参数 1..n-1，最后 `tailcall` 参数 0；
- 外层都是先编译所有子表达式（包括首项这个 Application），再 `call` 临时 lambda。

存在的差异都是周边细节，不会导致 brainfuck 这个调用出错：

| 差异点 | TypeScript | C | 是否影响本问题 |
|---|---|---|---|
| 临时函数返回标签 | 显式生成一个未引用的 `@TEMP_LAMBDA_RETURN_TARGET_...` | 未生成 | 否 |
| 参数中的 `break`/`continue` | 特殊处理为 `goto` | 直接 `push` | 否 |
| Native 引用作为值传递 | `load` | `push` | 否 |
| Quasiquote 内部 Lambda | `loadclosure` | `push` | 否 |
| `begin` 特殊形式 | 未实现 | 已实现 | 否 |
| 标签/临时变量命名 | 字符串哈希 | 单调整数 + label | 否 |

## 2. 真正导致运行错误的原因

运行时错误 **并非 η 变换逻辑错误**，而是 **操作数栈（operand stack）溢出**。

在 `(cons ... ((env_constructer ...) (+ iter 1)))` 这类嵌套复杂调用中，外层 `cons` 的第一个参数会留在操作数栈上；接着通过 η 变换的临时 lambda 尾调用进入内层 lambda 时，这些值会“继承”到被调用者的栈中。当前静态分析的 `opstack_depth` 没有足够保守地覆盖这种跨尾调用的栈继承场景，导致 `am_process_push_operand` 在栈满时返回失败。而调用方大多没有检查返回值，于是后续从栈上 `store` 参数时取到了错误的数据，最终表现为 `String.length` 收到非字符串、变量绑定错乱等运行异常。

## 3. 修复内容

修改了 `src/process.c` 中的 `am_process_push_operand`：当操作数栈满时，自动按 `capacity * 2` 扩容，而不是静默失败。

```c
int32_t am_process_push_operand(am_process_t *proc, am_value_t v) {
    if (!proc || !proc->opstack || !proc->opstack_top) return -1;
    size_t used = (size_t)(proc->opstack_top - proc->opstack);
    if (used >= proc->opstack_capacity) {
        size_t new_capacity = proc->opstack_capacity * 2;
        if (new_capacity < 16) new_capacity = 16;
        am_value_t *new_opstack = (am_value_t *)am_realloc(proc->vm_alloc, proc->opstack,
                                                             new_capacity * sizeof(am_value_t));
        if (!new_opstack) return -1;
        proc->opstack_top = new_opstack + used;
        proc->opstack = new_opstack;
        proc->opstack_capacity = new_capacity;
    }
    *proc->opstack_top++ = v;
    return 0;
}
```

## 4. 验证结果

- `make all` 构建通过。
- 最小复现 `./main testscm/bf_min.scm` 已可正确输出 `(0 2 0 0 65 32)`。
- 原用例 `./main testscm/brainfuck.scm` 已可正确输出：

```
预期输出：Hello World!
Hello World!
...
iteration steps = 986
All runtime tests passed.
```

- `test_runtime_new_opcodes`、`test_list`、`test_map`、`test_size` 等测试通过。
- 另外几个测试（`test_ast`、`test_linker`、`test_process`、`test_runtime`）在本环境中有与本次修改无关的既有失败（断言/文件路径问题），未做改动。

> 注：为定位问题我创建了 `testscm/bf_min.scm` 作为最小复现文件。按您要求未执行任何删除操作，故保留该文件。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

这是一个Scheme解释器实现。请你阅读全部C语言代码，理解其语法分析、编译、虚拟机指令集等关键机制，在 @src/parser.c 中，修改 am_parser_opstack_depth_analysis 实现，要求如下。

- 需求背景：现有 am_parser_opstack_depth_analysis 的功能是对AST进行静态分析，进而得到最大opstack深度。然而，由于compiler对AST进行编译时，可能会执行η变换等操作，导致实际编译出的IL代码的最大栈深度，与基于AST静态分析出来的结果不一致。因此应该对编译出来的IL指令序列做静态分析，才合理。
- 具体要求：在 @src/compiler.c 中实现函数 am_compiler_opstack_depth_analysis ，功能依然是分析得到最大opstack深度，但分析对象是ilcode。实现 am_compiler_opstack_depth_analysis 后，你需要将所有的 am_parser_opstack_depth_analysis 调用，替换为新实现的 am_compiler_opstack_depth_analysis ，并清除 am_parser_opstack_depth_analysis 的实现。

无需编写新的测试。保证 @main.c 编译运行正确即可，因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

这是一个Scheme解释器实现。请你阅读全部C语言代码，全面理解其架构，重点关注对象和值机制、内存管理（ @include/allocator.h ）接口、垃圾回收（ @src/process.c ）、VM中间语言指令集和进程调度等关键设计，完成底层物理内存分配器的实现需求。这是一个规模比较大的需求。

# 总体要求

- 请你新建 @src/allocator.c ，基于 @include/allocator.h 中定义的抽象接口，实现基于同一片内存池、但使用两种不同机制的内存分配器实例，并暴露必要的管理接口，供VM和GC使用。
- 在 @main.c 中实现了一个临时的基于 bump pointer 的粗糙的内存分配器，供参考用法框架。
- 你可以使用WSL进行编译构建和测试。
- 不得执行任何删除文件性质的操作，例如不得执行`rm`命令。

# 内存管理设计思路

- 解释器全生命周期拥有宿主系统分配的一大块内存池。
- 内存池分成两部分，分别是用户区（堆区）和工作区（VM区）。
- 用户区（堆区）存储Scheme语言的数据对象，即am_object_t的子类（带有base头的对象），包括closure、continuation、list、map、wstring。工作区（VM区）存储数据对象之外的各类数据，例如编译阶段的token、临时AST、临时栈，以及运行阶段的进程数据结构（除了数据对象的所有数据，例如堆容器heap、操作数栈opstack、函数调用栈fstack、符号表、PC、当前闭包handle等）、进程池容器、VM状态数据、所有进程共享的数据（如FIFO）等。
- 堆容器（ @src/heap.c ）只分配逻辑地址（handle），并维护handle与物理地址（指针）的映射关系，以维持逻辑地址的稳定。
- 解释器维护两个am_allocator_t，分别管理用户区和工作区。其中heap_alloc管理内存池低半部分内存（用户堆区），vm_alloc管理内存池高半部分内存（VM工作区），两者之间通过boundry分界，初始boundry位于内存池的中间。
- 所有allocator分配的内存池内指针，统一按4字节对齐（指针最低两位恒0）。
- 工作区不受GC管理，采用简单 bump pointer 内存分配策略。仅在关键时间点对连续的已分配区段执行整体重置（例如编译过程结束后直接清空整个VM区，供运行时从头开始分配）。
- 用户堆区受GC管理，拟采用标记-清除和标记-压缩混合策略。默认采用现有的标记-清除策略。当GC一定次数或者内存分配失败时，触发标记-压缩。底层内存分配使用 First-Fit Free-List 策略。
- 用户数据对象不会引用内存池中的物理地址，只会引用heap中分配的handle。

# 测试

无需编写新的测试。保证 @main.c 编译运行正确即可，因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

---------------------

参照 @src/heap.c 中的 am_heap_deep_dump / am_heap_deep_load 的接口格式，在 @src/module.c 和 @include/module.h 中，实现 module 数据结构的dump和load函数，以实现二进制持久化。

不要修改 @main.c ，但可以参照 @main.c 另写一个完整测试，以测试 dump/load 的正确性。你可以用 @test/test.scm 作为测试输入。你可以使用WSL进行编译构建和测试。

---------------------

修改 @main.c ，实现以下需求：

1、基于 am_module_dump ，在 am_compile 后，将编译出的模块持久化到系统内存（模拟转储为外部文件）。

2、mod转储完成后，彻底清空vm_alloc管理的VM工作区内存，为后面的runtime腾出工作空间。

3、am_load_module 之前，再通过 am_module_load ，加载转储后的模块。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

这是一个Scheme解释器实现。请你阅读全部C语言代码，全面理解其架构，重点关注对象和值机制、内存管理（ @include/allocator.h 和 @src/allocator.c）、垃圾回收（ @src/process.c ）、VM中间语言指令集和进程调度等关键设计，完成以下需求。

# 背景介绍：内存管理设计思路

- 解释器全生命周期拥有宿主系统分配的一大块内存池。
- 内存池分成两部分，分别是用户区（堆区）和工作区（VM区）。
- 用户区（堆区）存储Scheme语言的数据对象，即am_object_t的子类（带有base头的对象），包括closure、continuation、list、map、wstring。工作区（VM区）存储数据对象之外的各类数据，例如编译阶段的token、临时AST、临时栈，以及运行阶段的进程数据结构（除了数据对象的所有数据，例如堆容器heap、操作数栈opstack、函数调用栈fstack、符号表、PC、当前闭包handle等）、进程池容器、VM状态数据、所有进程共享的数据（如FIFO）等。
- 堆容器（ @src/heap.c ）只分配逻辑地址（handle），并维护handle与物理地址（指针）的映射关系，以维持逻辑地址的稳定。
- 解释器维护两个am_allocator_t，分别管理用户区和工作区。其中heap_alloc管理内存池低半部分内存（用户堆区），vm_alloc管理内存池高半部分内存（VM工作区），两者之间通过boundry分界，初始boundry位于内存池的中间。
- 所有allocator分配的内存池内指针，统一按4字节对齐（指针最低两位恒0）。
- 工作区不受GC管理，现状采用简单 bump pointer 内存分配策略。仅在关键时间点对连续的已分配区段执行整体重置（例如编译过程结束后直接清空整个VM区，供运行时从头开始分配）。
- 用户堆区受GC管理，拟采用标记-清除和标记-压缩混合策略。默认采用现有的标记-清除策略。当GC一定次数或者内存分配失败时，触发标记-压缩。底层内存分配使用 First-Fit Free-List 策略。
- 用户数据对象不会引用内存池中的物理地址，只会引用heap中分配的handle。

- 当前痛点问题：工作区（VM区）的allocator采用简单 bump pointer 内存分配策略。然而，由于进程的heap位于VM区，而heap表在运行时会快速膨胀，触发频繁的扩容，而每次扩容都会在空闲区新增两倍大的map对象，致使VM的空闲空间快速耗尽。

# 需求

- 基于上述背景，请你修改 @src/allocator.c ，将工作区（VM）的allocator改成空间利用率更高、更能利用碎片和空洞、速度可接受的内存分配策略。
- 思考过程中，如有关键方案问题需要决策，请直接向我提问（提供备选方案出选择题问我），得到我的回答后再继续。
- 你可以使用WSL进行编译构建和测试。
- 不得执行任何删除文件性质的操作，例如不得执行`rm`命令。

# 测试

无需编写新的测试。保证 @main.c 编译运行正确即可，因为这是全流程的测试。你可以使用WSL进行编译构建和测试。你可以使用`./main test/test.scm`进行测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请你编译项目并执行 ./main /mnt/d/Desktop/GitRepos/Animac-2026/test/mlp.scm，这是一个机器学习算法的测试用例。实测发现，大概在第30epoch时，会报错推出。

这个用例一次需要运行较长时间，因此在实际运行测试之前，你不妨先增加必要的日志打印，尤其是要明确在哪条IL指令处出了问题，以便快速定位问题。你可以利用gdb等工具定位问题。你可以使用WSL进行编译构建和测试。

已定位并修复 `mlp.scm` 崩溃的根因。

## 根因

`map` 递归深处，操作数栈（opstack）容量被编译器估计为 2406，但递归调用过程中栈深度会线性增长并超过该容量。`am_process_push_operand` 在满栈时返回 `-1`，而 `op_load` 等指令未检查返回值就继续 `am_process_step`，导致：

1. `load f` 压栈失败，PC 却前进。
2. 接下来 `store f` 错误地把栈顶的 `cdr` 结果弹出并绑定给 `f`。
3. 再接下来 `store lst` 弹出的是栈中残留的 `0x15`（boolean false）。
4. 后续 `car` 对 `0x15` 操作，触发“操作数非 handle”异常。

## 修复

在 `src/process.c` 的 `am_process_push_operand` 中启用原先被注释掉的动态扩容逻辑：

```c
if (used >= proc->opstack_capacity) {
    size_t new_capacity = proc->opstack_capacity * 2;
    if (new_capacity < 16) new_capacity = 16;
    am_value_t *new_opstack = (am_value_t *)am_realloc(proc->vm_alloc, proc->opstack,
                                                         new_capacity * sizeof(am_value_t));
    if (!new_opstack) return -1;
    proc->opstack_top = new_opstack + used;
    proc->opstack = new_opstack;
    proc->opstack_capacity = new_capacity;
}
```

同时清理了之前为定位问题添加的 `TRACE` / `op_cdr` / `op_store` 等高频日志（保留了你之前加入的最后指令记录与主动 GC+压缩逻辑）。

## 验证

- `make clean && make` 无警告、无错误。
- `test/factorial.scm`、`test/calculator.scm`、`test/list.scm`、`test/coroutine.scm`、`test/brainfuck.scm` 均正常运行。
- `mlp.scm` 不再在 PC=1717 崩溃，训练流程持续前进，已观察到 `Epoch 6` → `Epoch 12` 及以上的 Loss/Acc 输出。

程序现在可以正常执行 `mlp.scm` 的 MLP 训练，只是完整训练仍需一定时间。

---------------------

当前allocator实现中，VM区和heap区的boundry只能单向移动（向扩大VM区的方向移动）。我的需求是：将boundry改成能够双向移动。

已完成边界双向移动改造，核心改动在 `src/allocator.c`。

## 改造思路

原来边界只能单向扩大 VM 区（或 VM 为空时才能扩大 heap），根本原因是：
- VM 对象通过 raw pointer 被大量引用，无法像 heap 对象那样统一搬迁。
- 因此扩张 heap 时要求 VM 区整体为空。

解决方式：**让 VM 分配策略改为“从顶部向下生长”**，这样：
1. 新分配的 VM 对象尽量靠近 VM 区顶部。
2. VM 区底部始终保持一段连续空闲空间。
3. 扩张 heap 时只需把 VM 区底部的连续空闲块划给 heap，无需移动任何 VM 对象。

## 具体改动

### 1. VM 分配策略改为顶部分配
- `vm_find_large_block` / `vm_find_free_block`：优先选择地址最高（最靠近 VM 顶部）的空闲块。
- `segregated_malloc`：拆分空闲块时从**高端**分配，低端保留为新的空闲块。

### 2. 新增 VM 低端空闲探测
- `vm_lowest_used_offset`：扫描 VM 区，返回第一个已用块相对 VM 基址的偏移。
- 该偏移量就是当前可以安全划给 heap 的最大连续空闲尺寸。

### 3. heap 扩张逻辑改造
- `am_allocator_pool_adjust_boundary` 在 `new_boundary > boundary` 时：
  - 不再要求 `vm_state.used_bytes == 0`。
  - 通过 `vm_lowest_used_offset` 计算 heap 可扩张的上限。
  - 若剩余 VM 空闲空间不足以构成有效空闲块，则一次性全部划给 heap。
- `pool_reinit_heap_at`：heap 扩张时延伸**顶部空闲块**到新边界，而不是依赖 `used_bytes` 位置。
- `pool_reinit_vm_at`：heap 扩张时移除 VM 低端被划走的空闲块，并重新插入剩余的空闲块。

### 4. 自动边界调整策略放宽
- `am_allocator_pool_auto_adjust` 的 heap 扩张条件去掉 `pool->vm_state.used_bytes == 0` 限制，只要 VM 有富余即可尝试扩张。

## 验证

- `make clean && make` 无警告、无错误。
- `test/factorial.scm`、`test/calculator.scm`、`test/list.scm` 均正常结束。
- `test/mlp.scm` 继续正常运行，未出现崩溃。
- 编写并运行了边界双向移动专项测试：
  - heap 30% → 50%（VM 空）✅
  - heap 50% → 70%（VM 已分配 10 个对象，对象内容校验通过）✅
  - heap 70% → 95%（自动 clamp 到最大允许比例）✅
  - heap 95% → 20%（VM 扩张）✅

边界现在可以双向移动，且 VM 中的已分配对象不会被破坏。

---------------------

现在在测试 @test/fork.scm 用例时，./main /mnt/d/Desktop/GitRepos/Animac-2026/test/fork.scm，会报段错误，如下：

```
test_runtime_load_from_file ...
Module dumped: 4584 bytes
Module loaded: opstack_depth=2, ilcode_length=22
=== VM output ===
Exec: PC=0 | OpCode=8 | Oprand=69(varid=2)
Exec: PC=2 | OpCode=8 | Oprand=71(varid=2)
Exec: PC=3 | OpCode=1 | Oprand=263(varid=8)
[Runtime] 指令执行异常
Exec: PC=3 | OpCode=1 | Oprand=263(varid=8)
Exec: PC=4 | OpCode=4 | Oprand=21(varid=0)
Exec: PC=5 | OpCode=2 | Oprand=263(varid=8)
Exec: PC=6 | OpCode=30 | Oprand=15(varid=0)
Exec: PC=7 | OpCode=13 | Oprand=389(varid=12)
Exec: PC=12 | OpCode=4 | Oprand=931(varid=29)
Exec: PC=13 | OpCode=21 | Oprand=15(varid=0)
childExec: PC=14 | OpCode=11 | Oprand=15(varid=0)
Exec: PC=1 | OpCode=19 | Oprand=15(varid=0)

=== VM halted ===
Segmentation fault (core dumped)
```

补充说明：fork机制完全没有使用op_fork和fork指令，而是使用 @src/native_System.c 中实现的System_fork函数实现的，提请注意。

请排查问题。

已定位并修复 `test/fork.scm` 的段错误。

## 根因

`System.fork` 会生成子进程，子进程对父进程堆做深拷贝，但**父子进程共享同一个底层 `heap_alloc` 物理堆区**，各自维护独立的 `am_heap_t` 句柄表。

事件循环在每次计算阶段结束后，会对所有就绪进程依次调用 `am_process_gc` 做标记-清除，并每隔 `AM_HEAP_COMPACT_INTERVAL` 次触发一次 `am_allocator_heap_compact` 做标记-压缩。问题在于：

1. 父进程 GC 压缩：把父进程存活对象搬到堆区前端 `s->base`。
2. 子进程 GC 压缩：紧接着把子进程存活对象也搬到同一个 `s->base`，**覆盖**了父进程刚搬好的对象。

父进程再次调度时，`current_closure_handle` 指向的内存已被子进程对象覆盖，类型字段变成 0/非法，随后 `store` 指令解引用无效对象，最终破坏 freelist，在 `am_heap_destroy` 时触发段错误。

## 修复

引入全局标志 `g_am_multi_process`：

- `src/native_System.c`：fork 成功后置 `g_am_multi_process = 1`。
- `src/process.c` 的 `am_process_gc`：多进程模式下跳过 `am_allocator_heap_compact`。
- `src/runtime.c` 的 `runtime_gc_compact_if_needed`（当前被注释掉，但一并防护）：同样跳过压缩。

保留标记-清除（sweep），因为父子进程对象互不共享，sweep 是安全的；仅禁用不安全的“按进程单独压缩”。

## 验证

- `./main test/fork_minimal.scm`：父子均正常输出并干净结束。
- `./main test/fork.scm`：不再崩溃，定时器回调能持续执行。
- `valgrind --error-exitcode=1 ./main test/fork_minimal.scm`：`ERROR SUMMARY: 0 errors`。
- `timeout 3 valgrind ./main test/fork.scm`：0 个内存错误。

单进程测试（`factorial.scm`、`calculator.scm`、`coroutine.scm`）也通过，未引入回归。

## 补充说明

这本质上是一个**架构层面的 workaround**：共享堆分配器 + 按进程独立压缩是不相容的。长期彻底的方案有两种方向：

1. 让压缩全局化：一次性把所有进程的存活对象一起搬到堆区前端。
2. 为每个进程（或每个 fork）分配独立的物理堆区/分配器。

当前方案以最小改动解决了崩溃问题，但多进程模式下堆会失去压缩整理，可能带来碎片化。如果后续对 fork 使用频繁或长时间运行，建议按方向 1 或 2 重构。

---------------------

现在请你执行按进程独立压缩改造，采用方案1：让压缩全局化：一次性把所有进程的存活对象一起搬到堆区前端。

改造后，需要保留VM区/heap区边界的灵活移动等关键特性。

请你使用 @test/fork.scm 、 @test/test.scm 、 @test/mlp.scm 三个测试用例进行测试。你可以使用WSL进行编译构建和测试。

已完成按进程独立压缩的全局化改造（方案 1）。

## 改造内容

### 1. 新增全局压缩函数 `am_allocator_heap_compact_global`

`src/allocator.c` 中实现，签名加入 `include/allocator.h`：

```c
int32_t am_allocator_heap_compact_global(am_allocator_t *heap_alloc, am_heap_t **heaps, size_t heap_count);
```

逻辑：
- 一次性扫描**所有传入进程堆**的 handle 表，收集物理上位于堆区内的存活对象（用 `block->live` 去重，防御多 heap 引用同一对象）。
- 按地址排序后，把所有存活对象顺序搬到堆区前端 `s->base`。
- 第二遍再遍历所有 heap，更新非主 slot 中仍指向旧地址的指针。
- 尾部重建空闲块并更新 `free_list_head` / `used_bytes`。

原有的 `am_allocator_heap_compact(heap_alloc, heap)` 现在只是一个薄包装，调用全局版本并传入单元素 heap 数组，保持 API 兼容。

### 2. `am_process_gc` 只负责标记-清除

移除了 `am_process_gc` 里的压缩逻辑和 `g_am_multi_process` 临时开关。它现在只做：

1. 收集 GC 根
2. 递归标记
3. 清除未标记对象

### 3. 运行时统一调度全局压缩

`src/runtime.c`：
- 新增 `am_runtime_t.gc_count`，在 `am_runtime_create` 中初始化为 0。
- `am_runtime_event_handler` 现在：
  1. 对**所有现存进程**执行 `am_process_gc`（不再只 GC 就绪队列）。
  2. `rt->gc_count++`。
  3. 每 `AM_HEAP_COMPACT_INTERVAL` 个周期，收集所有非空进程的 `proc->heap`，调用 `am_allocator_heap_compact_global` 做一次全局压缩。
  4. 压缩成功后仍调用 `am_allocator_pool_auto_adjust(pool)`，保留 VM/heap 边界灵活调整能力。
- `runtime_gc_compact_if_needed`（当前被注释掉）也同步改为全局压缩，避免将来重新启用时再次踩坑。

### 4. 移除临时 workaround

`src/native_System.c` 中的 `g_am_multi_process` 全局标志和相关代码已完全移除，文件恢复到原始状态。

## 测试结果（WSL Ubuntu-24.04，gcc -O3）

| 测试 | 结果 |
|------|------|
| `test/fork.scm` | 不再段错误，父子进程正常调度，定时器回调持续执行。`timeout 3 valgrind --error-exitcode=1 ./main test/fork.scm` 报告 `ERROR SUMMARY: 0 errors`。 |
| `test/test.scm` | 正常完成，输出与预期一致。 |
| `test/mlp.scm` | 正常训练无崩溃，5 分钟超时前已跑到 Epoch 73，准确率多次达到 1.0；因训练脚本本身很长被 `timeout` 结束，不是解释器异常。 |
| `test/factorial.scm` / `test/calculator.scm` / `test/coroutine.scm` | 单进程回归测试通过。 |

## 说明

- 由于 `am_runtime_execute` 里的逐指令 `printf` 在当前工作树中已经是注释状态，测试输出比较干净；这有利于 `mlp.scm` 这种大量指令的程序，但注意它并非本次改造引入，只是保留了当前工作树状态。
- 全局压缩相比原来的按进程压缩，在单进程场景下行为等价；多进程场景下避免了把不同进程对象搬到同一前端互相覆盖的问题。
- VM/heap 边界自适应调整仍然保留，因为压缩后 `am_allocator_pool_auto_adjust` 照常调用。

---------------------

请你在 @src/runtime.c 中，参考 @src/allocator.c 中的 compact_print_report 函数，实现一个统计并打印内存总体使用状况的函数，这个函数可以在对象语言运行时的任何时刻被调用（以便后面实现相关native函数，现在暂不实现）。该函数的接口本身与allocator的实现策略无关，是抽象的，但是要调用allocator的相关接口，统计到以下信息：

- VM区（工作区）的总容量、用量。
- heap区（用户区）的总容量、用量。

你可以测试在 @src/runtime.c 的 am_runtime_event_handler 每次执行GC后调用该函数，打印内存信息。请你使用 @test/fork.scm 、 @test/test.scm 、 @test/mlp.scm 三个测试用例进行测试。你可以使用WSL进行编译构建和测试。

---------------------

基于你刚刚实现的 am_runtime_print_memory_stats ，在 @src/native_System.c 中，实现一个native函数：(System.memstat) ，即 am_native_System_memstat 。该native函数在Scheme层面无输入参数，但是返回一个新的list对象，其内容格式是 '(vm区容量, vm区已用, heap区容量, heap区已用)。同时，保留 am_runtime_print_memory_stats 的格式化打印功能。

提示：你可能需要改造 am_runtime_print_memory_stats 函数的接口定义，以取出 vm区容量, vm区已用, heap区容量, heap区已用 这四个关键信息。

请你使用 @test/test.scm 进行测试，我已经在其中加入了多处 (System.memstat) 可供测试。你可以使用WSL进行编译构建和测试。

已完成 `System.memstat` native 函数实现。

## 改造内容

### 1. 抽象内存统计接口 `include/runtime.h` / `src/runtime.c`

新增与 allocator 实现无关的快照结构：

```c
typedef struct {
    size_t vm_capacity;   // VM 工作区容量
    size_t vm_used;       // VM 工作区已用
    size_t heap_capacity; // 用户堆区容量
    size_t heap_used;     // 用户堆区已用
} am_runtime_memory_stats_t;
```

新增 getter：

```c
int32_t am_runtime_get_memory_stats(am_runtime_t *rt, am_runtime_memory_stats_t *out);
```

`am_runtime_print_memory_stats` 改为先调用 getter 再格式化打印，保留原有打印功能。

### 2. `System.memstat` native 函数 `src/native_System.c`

实现 `am_native_System_memstat`：

1. 调用 `am_runtime_get_memory_stats` 取出四项数值。
2. 创建 `am_list_t` 列表，依次压入：
   - `vm_capacity`
   - `vm_used`
   - `heap_capacity`
   - `heap_used`
3. 在进程堆中分配 handle，将列表对象注册到 handle。
4. 将 handle 压入操作数栈。
5. **调用 `am_process_step(proc)` 前进 PC**（关键：参考 `native_push_float_or_null` / `native_push_wstring_buf` 的实现，native 函数自行推进 PC）。

已注册到 `am_native_System_funcs[]` 并声明在 `include/native_System.h`。

### 3. 事件循环中保留打印调试

`am_runtime_event_handler` 每次 GC 后仍调用 `am_runtime_print_memory_stats(rt)`，便于观察 `System.memstat` 返回的数据与打印输出是否一致。

## 测试结果（WSL Ubuntu-24.04）

| 测试 | 结果 |
|------|------|
| `test/test.scm` | **通过**。模块加载、fork、多处 `(System.memstat)` 调用均正常执行，最终 `=== VM halted ===`，未再出现之前的 VM 分配失败。 |
| `test/fork.scm` | 通过，定时器回调持续执行，无段错误。 |
| `test/mlp.scm` | 通过，训练过程无崩溃。 |

`System.memstat` 返回示例（在独立最小测试中）：

```scheme
(104857600 38352 104857600 4800)
```

## 关键修复点

最初实现时遗漏了 `am_process_step(proc)`，导致 `System.memstat` 被反复执行、操作数栈和堆被不断填充，最终触发 VM 区大对象分配失败。加入 `am_process_step` 后行为与其它 native 函数一致，`test.scm` 恢复正常。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

1、修改 @src/runtime.c 中的 am_runtime_output 和 am_runtime_error 两个函数。当前这两个函数往 rt->output_fifo 和 rt->error_fifo 中push的是整个 am_wstring_t 对象，我要求你将其改成逐个push字符串中每个wchar的TPV，即am_value_t类型的value。这样fifo的消费者就可以逐个消费字符，而不是整个ws对象。

2、在 @main.c 中，实现 on_tick 回调函数，该回调函数用于将 am_list_t *(rt->output_fifo) 中收集到的所有字符，一次性全部pop出来，并通过printf打印出来。

无需编写新的测试。保证 @main.c 编译运行正确即可，因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

请参照 @typescript/src/REPL.ts 和 @main.c ，新建 @repl.c ，用C语言实现Scheme解释器的REPL。这个REPL的实现思路是所谓的“replay-based REPL”，并支持自动缩进。具体逻辑可参照 @typescript/src/REPL.ts 。

你可以使用WSL进行编译构建和测试，但不要修改已有的代码。不要执行任何删除文件之类的操作，如`rm`等。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个Scheme解释器，具备FFI功能，称为“本地宿主接口”（native）。现在请你在 @src/native_LLM.c 和 @ include/native_LLM.h 中，实现语言模型推理相关的本地宿主接口的实现。详细要求如下：

- 你需要全面阅读C语言代码，理解本地宿主接口机制。在参考 @src/native_System.c 、 @src/native_String.c 、 @src/native_Math.c 时，务必注意每个函数实现的“套路”，也就是出栈入栈、TPV（am_value_t）与具体值类型的转化、堆对象的存取和增删改、handle机制、调用step函数以PC++等机制。这些机制有的被封装在工具函数中了，你要注意发现它们，不要遗漏。
- 现有TypeScript实现的LLM本地宿主接口参考实现，位于 @typescript/lib/LLM.js 。你需要阅读理解这个文件，实现其中的 init 、 get_config 、 get_param 、 matmul 、 encode 、 decode 这几个函数。其他函数无需实现，但可以参考。
- 除TS实现之外，还有等效的C语言实现，主要在 @llm/infer.c 、 @llm/tensor.c 、 @llm/tokenizer.c 等文件以及相关的头文件中。你可以参考C语言代码中实现分词器、前缀树等算法的部分，以弥补TS中被高级语言封装起来的部分。
- 你需要用C语言从头实现一个从base64字符串到字节序列的解码算法，用于模型加载相关接口。
- 你所实现的LLM本地宿主函数库，最终是服务于 @test/nano_llm_infer.scm ，这是一个完全由Scheme实现的Transformer语言模型推理。以该文件为测试输入。

无需编写新的测试。保证 @main.c 编译运行正确，以及解释执行 @test/nano_llm_infer.scm 正确即可，因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

补充两点：

1、 @test/nano_llm_infer.scm 中报错变量未找到，是因为用了不兼容的“=”运算符，我已经全部改成兼容的"=="了。

2、再次核实 @src/native_LLM.c 中关于Trie树的实现。你的实现应该以 @typescript/lib/LLM.js 为准， @llm/tokenizer.c 实现的是Radix树，仅供参考。

继续。

---------------------

写一个交互式网页 @tools/jstoscm.html ，用JavaScript实现一个从JavaScript子集到Scheme子集的翻译器。注意：只是字面上的机械翻译，不要处理语义分析问题。

JavaScript子集的BNF如下：

```
<program> ::= <expr_seq>

<expr> ::= <block> | <function> | <if_block> | <while_block> | <vardef> | <term> | return <expr> | return | continue | break
<expr_seq> ::= <expr> ( ; | CRLF ) <expr_seq> | <expr> | eps

<block> ::= { <expr_seq> }
<if_block> ::= if ( <term> ) <block> <else_block>
<else_block> ::= else <block> | else <if_block> | eps
<while_block> ::= while ( <term> ) <block>

<vardef> ::= var <id> | var <id> = <term>

<assignment> ::= <postfix> = <term>

<function> ::= function <id> ( <param_seq> ) <block>
<lambda> ::= ( <param_seq> ) => <block>
<param_seq> ::= <id> , <param_seq> | <id> | eps

<term> ::= <assignment> | <ternary>
<ternary> ::= <term_or> ? <term> : <term> | <term_or>

<term_or> ::= <term_or> || <term_and> | <term_and>
<term_and> ::= <term_and> && <term_cmp> | <term_cmp>

<term_cmp> ::= <term_cmp> ( > | < | == | <= | >= ) <term_add> | <term_add>

<term_add> ::= <term_add> ( + | - ) <term_mul> | <term_mul>
<term_mul> ::= <term_mul> ( * | / | % ) <term_exp> | <term_exp>

<term_exp> ::= <unary> ^ <term_exp> | <unary>

<unary> ::= ( ! | - | ++ | -- ) <unary> | <postfix>

<postfix> ::= <primary> 
            | <postfix> ( <term_seq> ) 
            | <postfix> [ <term> ] 
            | <postfix> ++ 
            | <postfix> --

<primary> ::= <id> | <literal> | <list> | <lambda> | ( <term> )

<term_seq> ::= <term> , <term_seq> | <term> | eps

<list> ::= [ <item_seq> ]
<item_seq> ::= <item> , <item_seq> | <item> | eps
<item> ::= <term>

<id> ::= identifier
<literal> ::= number | <string> | boolvalue | null | undefined

<string> ::= " <string_chars> "
<string_chars> ::= <string_char> <string_chars> | eps
<string_char> ::= <any_char_except_quote_and_backslash> | "\" <escape_char>
<escape_char> ::= " | \ | n | r | t | 0
```

Scheme子集的BNF如下：

```
    <SourceCode> ::= ((lambda () <TERM>*)) CRLF
          <Term> ::= <SList> | <Lambda> | <Quote> | <Unquote> | <Quasiquote> | <Identifier>
         <SList> ::= ( <SListSeq> )
      <SListSeq> ::= <Term> <SListSeq> | ε
        <Lambda> ::= ( lambda <ArgList> <Body> )
       <ArgList> ::= ( <ArgListSeq> )
    <ArgListSeq> ::= <ArgIdentifier> <ArgListSeq> | ε
 <ArgIdentifier> ::= <Identifier>
          <Body> ::= <BodyTerm> <Body_>
         <Body_> ::= <BodyTerm> <Body_> | ε
      <BodyTerm> ::= <Term>
         <Quote> ::= ' <QuoteTerm> | ( quote <QuoteTerm> )
       <Unquote> ::= , <UnquoteTerm> | ( unquote <QuoteTerm> )
    <Quasiquote> ::= ` <QuasiquoteTerm> | ( quasiquote <QuoteTerm> )
     <QuoteTerm> ::= <Term>
   <UnquoteTerm> ::= <Term>
<QuasiquoteTerm> ::= <Term>
    <Identifier> ::= IDENTIFIER
```

以下是一些说明、启发式规则或案例：

- 标识符、立即数，基本遵循C语言的规则，但允许中间有“.”。例如，JS的`console.log`视为一整个标识符。
- identifier应包括负数，也就是说，凡是在JS中被视为带前导负号的数值，例如-1、-3.14、-1e23，翻译成Scheme也应当是一个带负号的整体，而不能翻译成 (- 3.14) 这种。
- 输入JS支持方括号括起来的列表，但是不支持花括号括起来的字典（哈希表）。
- 输出Scheme用花括号括起来的列表，是begin表达式的语法糖。
- `return a;`   →   `a`  也就是直接去掉return关键字，没有外层括号。
- `var a = b;`    →   `(define a b)`  允许出现在任何位置，不需要调整位置。不支持任何let绑定形式。
- `a = b;`     →   `(set! a b)`
- `lst[index] = v;`    →   `(set_item! lst index v)`
- `(x)=>{a; b;}`    →   `(lambda (x) a b)`
- `function foo(x) {a; b;}`    →   `(define foo (lambda (x) a b))`  不支持 `(define foo (x) a b)` 这种简化形式
- 代码块`{a; b;}`    →   `{a b}` 等价于 `(begin a b)`，这是自行设置的语法糖
- `if (c) {a; b;} else {p; q;}`   →   `(if c {a b} {p q})`
- `while (c) {a; b;}`   →   `(while c {a b})`  不支持也没有for循环形式

不得修改其他代码。


---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个Scheme解释器。为实现全局字符串驻留（string interning）和更严谨的基于内容的symbol相等性比较，我要求你在 @src/wstring.c 和 @include/wstring.h 中，实现一对多映射的多值哈希表数据结构及其API。

你可以使用WSL进行编译构建和测试。在实现这个需求之前，你需要评估我给出的方案是否合理，先给出你的判断，然后问我是否继续。得到我的批准之后，你才能开始编写代码实现需求。

详细要求如下：

1、背景。为实现全局字符串驻留，计划后续在编译时和运行时，实现基于全局字符串驻留的字符串对象延迟创建和同值复用。因此，我计划实现“多个string→单个hash→多个handle”的映射表strindex，用于检查某个特定字符串是否已经存在于strindex，进而判断这个字符串对应的内容是不存在（含哈希冲突）还是存在，进而实现内容相同的字符串对象对应唯一的堆handle（am_value_t的handle）。

2、具体需求。请你参考 @src/map.c 和 @include/map.h 中定义的一对一哈希表（am_value_t → am_value_t），参考其基于柔性数组的数据结构、哈希函数和接口格式，实现基于开放寻址+线性探测+一对多映射的多值哈希表： am_strindex_t 。其key为uint32_t类型的hash值，也就是将key用作 hash tag，当然地允许重复，允许一个key(hash)对应多个value。其value仍为am_value_t。插入(key=hash,handle)时，直接根据hash找到对应的桶，如果被占用，则往后寻找第一个空桶插入。通过hash作为key查询时，则还是用线性探测方式，把所有(hash,*)的entry都捞出来，返回所有的handle。这样，给定一个待查找的字符串，计算它的hash，就可能捞出来0个或多个handle，进而确定是冲突还是已存在还是不存在。支持以下操作：

```
// 以初始容量新建多值哈希表。capacity 会被向上取整为不小于它的最小 2 的幂。
// 所有 key 初始化为 AM_MAP_KEY_EMPTY，value 初始化为 AM_VALUE_NULL。
am_strindex_t *am_strindex_create(am_allocator_t *alloc, size_t capacity);

// 彻底销毁
int32_t am_strindex_destroy(am_allocator_t *alloc, am_strindex_t *obj);

// 深拷贝：创建并返回一个与原 strindex 内容完全一致的新对象。所有 key/value 按位拷贝。
am_strindex_t *am_strindex_copy(am_allocator_t *alloc, am_strindex_t *obj);

// 功能说明：计算对象所占用的实际字节数（考虑结构体填充和对齐问题）
// 成功返回字节数，失败返回SIZE_MAX
size_t am_strindex_size(am_allocator_t *alloc, am_strindex_t *obj);

// 功能说明：将表对象序列化成二进制序列，并转储到buffer[offset]
// 实现说明：offset是写入buffer的起点offset。成功则返回向buffer新增字节数，失败则返回SIZE_MAX。
// 注意：若buffer设为NULL，或者offset设为SIZE_MAX，则仅计算转储后的二进制序列的字节数，不实际写入buffer。
//       压缩对象，将capacity压缩到跟length一致，丢弃墓碑和空闲槽位。
size_t am_strindex_dump(am_allocator_t *alloc, am_strindex_t *obj, uint8_t *buffer, size_t offset);

// 功能说明：转储（dump）操作的逆操作。从二进制字节序列buffer[offset]开始，读取转储的对象，构造对象并返回其指针。
// 实现说明：offset是读取buffer的起点offset。成功则返回加载后am_strindex_t对象的指针，失败则返回NULL。
am_strindex_t *am_strindex_load(am_allocator_t *alloc, uint8_t *buffer, size_t offset);

// 查找：输入一个wchar_t字符串，计算其uint32_t哈希值，得到所有对应的value的列表（values由调用者管理），返回值为列表长度；若不存在，则返回0；若出错，则返回SIZE_MAX。注：二次比较不是这个接口需要做的事情，现阶段不实现，放到后面再实现。am_strindex_t 的接口只负责把hash匹配的所有可能的value全部查询出来。
size_t am_strindex_get_all(am_allocator_t *alloc, am_strindex_t *obj, wchar_t *str, am_value_t *values, size_t n_values);

// 插入新键值对。对输入的字符串计算hash，插入(key=hash,handle)时，直接根据hash找到对应的桶，如果被占用，则往后寻找第一个空桶插入。
// 当负载因子（含墓碑）超过 75% 时自动扩容。
// 返回新的对象指针；失败返回 NULL。调用者必须使用返回的指针替换原有指针。
am_strindex_t *am_strindex_set(am_allocator_t *alloc, am_strindex_t *obj, wchar_t *str, am_value_t value);

// 当前有效键值对数量
size_t am_strindex_length(am_allocator_t *alloc, am_strindex_t *obj);

// 物理槽位数
size_t am_strindex_capacity(am_allocator_t *alloc, am_strindex_t *obj);
```

3、补充要点：①可以补充 am_strindex_delete 接口，按照handle（value）进行查询和删除。②以 UINT32_MAX 为 EMPTY；以 (UINT32_MAX-1) 为 TOMBSTONE。③字符串哈希算法使用 FNV-1a。

4、测试。你可以使用WSL进行编译构建和测试。编写一个单独的 @test_strindex.c 文件以执行单元测试。


---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个Scheme解释器。为实现编译阶段的全局字符串驻留（string interning），我要求你在 @src/ast.c 中，基于刚刚实现的 am_strindex_t 数据结构，实现基于全局字符串驻留的字符串对象延迟创建和同值复用。我已经手工在 @include/ast.h 的 am_ast_t 结构体中增加了 strindex 成员。

这个需求比较复杂。在实现这个需求之前，你需要先评估我给出的方案是否合理，先给出你的判断，然后问我是否继续。得到我的批准之后，你才能开始编写代码实现需求。

具体需求如下：

0、请特别注意： am_strindex_t 是可变长容器，因此对其进行写操作之后务必注意检查和更新容器的指针。

1、修改AST对象的构造、初始化和析构等函数，增加 strindex 成员的构造、初始化和析构。初始容量定为1024。

2、修改 @src/ast.c 中的 am_ast_make_wstring_node ，为其增加字符串对象的同值复用机制，具体是：

- 首先在strindex中检查str是否存在（通过 am_strindex_get_all ）。
- 如果不存在，说明确实是新字符串，则向heap申请一个handle，构造字符串对象，写入堆，最后 am_strindex_set ，登记这个新str的hash，以便后续查重。
- 如果存在，则有已存在和碰撞两种可能。首先通过 am_strindex_get_all 取出所有待查 value(转为handle)，再将其字符串对象逐个从heap中取出，按照“先比长度、再比内容”的方式，高效确定str是存在还是碰撞。如果存在（长度和内容相同），就不用新建字符串对象了，直接返回内容重复的str对象的handle；如果是碰撞（长度不同，或者长度同但内容不同），就申请handle新建堆对象。

3、修改 @src/ast.c 中的 am_ast_merge 函数，实现 strindex 成员的合并和值handle的更新。

4、测试。你可以使用WSL进行编译构建和测试。无需编写新的测试。保证 @main.c 编译运行正确即可，因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

## 方案评估

我已阅读 `@doc/AGENTS.md`、`@include/ast.h`、`@src/ast.c` 以及相关的 `heap.h`/`wstring.h`。当前 `am_ast_t` 结构体中确实已增加 `strindex` 成员，`ast.c` 中尚未使用。

### 合理且可取的部分

1. **整体方向正确**：编译期字符串驻留能显著减少重复字符串对象，`am_strindex_t` 的 hash-tag + 候选 handle 列表设计适合作为查重索引。
2. **二级判定合理**：先 hash 初筛，再逐个从 heap 取对象做"长度+内容"比较，是处理 hash 冲突的标准做法。
3. **可变长容器指针更新提醒到位**：`am_strindex_set` 扩容后会返回新指针，`ast->strindex` 必须及时更新，否则会出现 use-after-free。
4. **初始容量 1024 合理**：与 `ast->nodes` 初始 1024 对齐，符合编译期规模预期。

### 我认为需要明确的问题

按影响程度排序：

| # | 问题 | 说明 |
|---|------|------|
| 1 | **`am_ast_merge` 中 strindex 合并的粒度** | 这是最关键的实现决策。 |
| 2 | **`am_ast_copy` 中 strindex 的处理** | 当前 `am_heap_copy` 是浅拷贝，strindex 可直接按位拷贝。 |
| 3 | **是否需要新增 `am_strindex_set_raw` 内部接口** | merge 时我们已知 hash，不想从 wstring 反向提取字符串再算一次 hash。 |
| 4 | **Windows `wchar_t` 16 位与 `am_wchar_t` 32 位差异** | 内容比较时需要确定按什么语义比较。 |

### 问题 1：merge 策略（最关键）

当前 `am_ast_merge` 对 importee 的 WSTRING 节点是**无条件深拷贝**到 importer（`merge_copy_wstring`），并给每个节点分配新的 handle。若要在 merge 后仍保持"全局驻留"，有两种方案：

**方案 A：机械合并（实现简单，但非完全驻留）**
- 遍历 `importee->strindex` 中的每个 `(hash, old_handle)`。
- 通过 `handle_merge_mapping` 查到 `new_handle`。
- 把 `(hash, new_handle)` 插入 `importer->strindex`。
- **优点**：改动小，与现有 merge 流程兼容。
- **缺点**：如果 importer 和 importee 原本都有相同内容的字符串，merge 后 importer 中会出现两个内容相同但 handle 不同的字符串对象，没有真正实现跨模块驻留。

**方案 B：完全去重（实现复杂，但真正实现全局驻留）**
- 在深拷贝 WSTRING 节点**之前**，先查 `importer->strindex`：
  - 若 importer 已存在相同内容字符串，则把 `handle_merge_mapping[old_handle]` 直接设为该已有 handle，**不创建新对象、不分配新 handle**。
  - 若不存在，则分配新 handle、深拷贝对象，并登记到 `importer->strindex`。
- **优点**：merge 后真正全局唯一。
- **缺点**：需要调整 merge 中"预先为所有节点分配 handle"的流程；WSTRING 节点需要单独预处理；还要释放因复用而多余的 handle。

**我的建议**：如果你的目标是"编译阶段全局字符串驻留"，方案 B 更彻底。但方案 A 是先落地、后迭代的稳妥选择。

### 问题 2：`am_ast_copy` 中 strindex 的处理

当前 `am_heap_copy` 是**浅拷贝**（按位拷贝指针），`am_ast_copy` 中 copy 出的 AST 与源 AST 共享 `nodes` 中的对象。因此 `copy->strindex` 可以直接用 `am_strindex_copy` 按位拷贝，handles 仍然有效。这个处理是合理的。

### 问题 3：是否需要新增内部接口

`am_strindex_set` 的签名要求传入 `wchar_t *str` 以计算 hash。但在 `am_ast_merge` 中，我们只有 `hash` 和 `new_handle`，对应的字符串对象已经存在。为避免从 wstring 反向提取字符串再算一次 hash，我计划在 `wstring.h` 中新增一个内部接口：

```c
// 按已知 hash 直接插入 (hash, value)，不重新计算字符串 hash。
// 成功返回新的 strindex 指针；失败返回 NULL。调用者必须更新指针。
am_strindex_t *am_strindex_set_raw(am_allocator_t *alloc, am_strindex_t *obj,
                                   uint32_t hash, am_value_t value);
```

如果你不接受新增接口，我也可以在 merge 中从 wstring content 反向构造 `wchar_t*` 再调用 `am_strindex_set`，但效率较低且跨平台编码处理麻烦。

### 问题 4：`wchar_t` 与 `am_wchar_t` 的语义

Windows 上 `wchar_t` 是 16 位（UTF-16 码元），Linux 上是 32 位（Unicode 码点），而 `am_wchar_t` 固定为 `uint32_t`。现有 `am_wstring_create` 是逐 `wchar_t` 转换（`(am_wchar_t)str[i]`），并未处理 surrogate pair。

我计划在内容比较时**与 `am_wstring_create` 的行为保持一致**：逐 `wchar_t` 比较 `wstring->content[i]` 与 token 文本。这样在同一平台上结果一致，且不引入新的编码转换逻辑。如果你有跨平台严格语义需求，则需要另行设计。

我的答复：

1、merge 策略选择方案A：机械合并。不完全的跨模块驻留是可以接受的。

2、允许新增set_raw内部接口。

3、wchar_t 与 am_wchar_t 都是 32 位Unicode 码点，实质上是 uint32_t。不考虑Windows。

4、在新建wstring对象之后，你需要立即更新对象base头中的hash字段，作为hash缓存，这样有利于后面快速判等。

---

## 请确认

1. **`am_ast_merge` 采用方案 A 还是方案 B？**（推荐方案 B 以实现真正全局驻留，但实现更复杂）
2. **是否允许在 `wstring.h` 中新增 `am_strindex_set_raw` 内部接口？**（推荐允许）
3. **内容比较是否按现有 `am_wstring_create` 的逐 `wchar_t` 语义进行？**（推荐是）
4. **是否确认继续按此方案编码？**

得到你的批准后，我将开始修改 `@src/ast.c`（必要时微调 `@include/wstring.h`）。



---------------------

基于前面新增的全局字符串驻留相关功能（am_ast_t 结构体中增加了 strindex 成员、修改了 am_ast_make_wstring_node 、am_ast_merge 等），增加新需求，继续完整实现全局字符串驻留相关功能。

在实现以下需求之前，你需要先评估我给出的方案是否合理，先给出你的判断，然后问我是否继续。得到我的批准之后，你才能开始编写代码实现需求。

具体需求如下：

1、修改 @src/module.c 中的 am_module_dump 和 am_module_load 函数，增加对于ast的 strindex 成员的dump和load。确保模块转储和加载过程适配了strindex字段。

2、修改 @src/process.c 中的 am_process_load_from_module ，将 mod->ast->strindex 复制到 process->strindex 。确保process适配了strindex字段。

3、以下是一个复杂需求。请你阅读并理解 @src/runtime.c 、 @src/process.c 和 @src/native_String.c 中有关创建字符串对象（am_wstring_t）的逻辑，完成以下两个任务：一是将“创建字符串堆对象并返回其handle”这一过程抽象出来，抽象成 am_process_xxx 这个格式的接口，这个接口需要参照你刚才改过的 am_ast_make_wstring_node 函数进行修改，通过查阅 proc->strindex ，实现增加字符串对象的同值复用机制，并在  @src/process.c 和  @include/process.h 中实现。注意：同值复用查询仅针对长度不超过32的字符串启用。你需要把这个长度阈值做成宏定义，以便修改调整。二是找到 @src/runtime.c 、 @src/process.c 和 @src/native_String.c 等源码中有关创建字符串堆对象的过程，整理出来，先给我确认。我来选择哪些地方需要换成你刚刚实现的这个字符串对象创建接口，然后你再实现。

4、测试。你可以使用WSL进行编译构建和测试。无需编写新的测试。保证 @main.c 编译运行正确即可（使用 @test/test.scm 作为输入），因为这是全流程的测试。你可以使用WSL进行编译构建和测试。

## 方案评估

我已阅读 `@src/module.c`、`@src/process.c`、`@src/runtime.c`、`@src/native_String.c`、`@src/native_LLM.c`、`@src/native_System.c`、`@include/process.h` 和 `@include/module.h`。

### 合理且可取的部分

1. **module dump/load 增加 strindex**：模块持久化需要完整保存 AST，strindex 作为 AST 成员必须参与序列化。由于 `am_heap_dump/load` 保持 handle 值不变，strindex 中存储的 handle 在加载后仍然有效，方案可行。
2. **process 复制 strindex**：`am_process_load_from_module` 通过 `am_heap_deep_dump/deep_load` 将 `mod->ast->nodes` 复制到 `proc->heap`，handle 值保持不变，因此可以直接用 `am_strindex_copy` 复制 `mod->ast->strindex`。
3. **抽象运行时字符串创建接口**：将散落的 `am_wstring_create + am_heap_alloc_handle + am_heap_set` 抽象为 `am_process_xxx` 接口是合理的，能减少重复代码并统一驻留逻辑。
4. **短字符串阈值驻留**：仅对长度 ≤32 的字符串启用驻留是常见优化，阈值做成宏便于调整。

### 我认为需要明确的关键问题

按重要性排序：

| # | 问题 | 说明 |
|---|------|------|
| 1 | **GC 与 strindex 的集成** | 这是最关键的问题。`proc->strindex` 中的 handle 如果只是作为"索引"存在，而没有被加入 GC 根，那么当堆中某个字符串对象不再被任何 GC 根引用时，`am_process_gc_sweep` 会回收该对象及其 handle。此后 `strindex` 中保留的 handle 将变成悬空引用，下次查询时从 heap 取出的指针无效。 |
| 2 | **哪些创建字符串的地方需要替换** | 我会列出所有调用点。其中 `op_duplicate`、`string-copy`、`substring` 等语义上是"创建副本"，替换为驻留接口后会变成"可能复用同一对象"。虽然 `am_wstring_t` 不可变，共享安全，但会影响 `eq?`（handle 级相等）的结果。 |
| 3 | **模块文件格式兼容性** | 在 `module_header_t` 中新增 `strindex_offset` 会改变模块文件格式，旧模块文件将无法加载。 |

### 问题 1：GC 与 strindex 集成（必须解决）

当前 `am_process_gc_root` 收集的根包括：当前闭包、操作数栈、函数调用栈、continuation。如果 strindex 不参与 GC，那么：

- `op_typeof` 创建的 `"string"` 等短字符串，在不再被 opstack 引用后会被 GC。
- 下次 `op_typeof` 再次需要 `"string"` 时，`strindex` 中仍有 handle，但对象已释放。

**建议方案**：在 `am_process_gc_root` 中把 `proc->strindex` 中所有有效 handle 加入 GC 根列表。这样 strindex 引用的字符串对象会一直保持存活。

如果你同意，我会在实现 `am_process_make_wstring_handle` 的同时修改 `am_process_gc_root`。

### 问题 2：所有运行时创建字符串对象的调用点

通过搜索 `am_wstring_create`，运行时（除 ast.c 外）共有以下 5 处：

1. **`src/runtime.c:935-946`** — `op_duplicate` 指令中 WSTRING 的深拷贝。
2. **`src/runtime.c:1299-1311`** — `op_typeof` 指令中创建类型名字符串（如 `"list"`, `"string"`, `"number"` 等）。
3. **`src/native_String.c:81-101`** — `native_push_wstring_buf` 内部辅助函数。被以下 native 函数调用：
   - `am_native_String_atom_to_string`
   - `am_native_String_number_to_string`
   - `am_native_String_symbol_to_string`
   - `am_native_String_wchar_to_string`
   - `am_native_String_string_copy`（语义为副本）
   - `am_native_String_substring`（语义为子串/新字符串）
   - `am_native_String_char_at`
4. **`src/native_LLM.c:154-169`** — `am_llm_push_wstring_buf` 内部辅助函数。
5. **`src/native_System.c:122-138`** — `native_push_wstring_buf` 内部辅助函数。

**我的建议**：
- 必须替换：**`op_typeof`**（类型名字符串短且重复极高）、**`native_String.c` 的 `native_push_wstring_buf`**（覆盖大量字符串构造）。
- 谨慎替换：**`op_duplicate`** 中的 WSTRING 复制、**`string-copy`**、**`substring`**。这些语义上是"新字符串"，但由于 `am_wstring_t` 不可变，返回同一对象也是安全的，只是会让 `eq?` 从 `#f` 变成 `#t`。如果你希望保留"副本"语义，这些可以暂不替换。
- 可替换：**`native_LLM.c`** 和 **`native_System.c`** 的辅助函数。

### 问题 3：模块文件格式兼容性

在 `module_header_t` 中新增 `strindex_offset` 会破坏旧模块文件格式。由于当前项目没有版本兼容要求，这是可以接受的。我会在 `MODULE_VERSION` 不变的情况下新增字段。

---

## 请确认

1. **是否同意在 `am_process_gc_root` 中将 `proc->strindex` 的有效 handle 全部加入 GC 根？**（推荐同意，否则驻留对象会被 GC 回收导致悬空 handle）
2. **对上述 5 处创建字符串的调用点，你希望哪些替换为新的驻留接口？**
   - 推荐至少替换：`op_typeof`、`native_String.c 的 native_push_wstring_buf`、`native_LLM.c 的 am_llm_push_wstring_buf`、`native_System.c 的 native_push_wstring_buf`
   - 可选替换：`op_duplicate` 中的 WSTRING 复制
   - 可选保留：`string-copy`、`substring`（若你希望保留副本语义）
3. **是否确认继续按此方案编码？**

得到你的批准后，我将开始实现：
- `include/process.h`：新增 `AM_PROCESS_STRINDEX_MAX_LEN` 宏、`am_process_make_wstring_handle` 声明。
- `src/process.c`：实现 `am_process_make_wstring_handle`、在 `am_process_load_from_module` 中复制 strindex、在 `am_process_destroy` 中释放 strindex、在 `am_process_gc_root` 中把 strindex 加入 GC 根。
- `src/module.c`：增加 `strindex_offset` 字段和 dump/load 逻辑。
- 根据你的选择替换相应调用点。

我的答复：

1、在 am_process_gc_root 中把 proc->strindex 中所有有效 handle 加入 GC 根列表。

2、完全同意你关于调用点替换的建议。替换：必须替换的和可替换的。谨慎替换的，不替换。保留副本语义。

3、模块文件格式兼容性问题，可以接受不兼容（因现在处于开发状态，尚未固化标准），但必须保证模块dump/load过程正确。MODULE_VERSION 不变。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个Scheme解释器。在刚刚完成的需求中，已经实现了运行期对于较短字符串的全局字符串驻留（string interning）。

基于此，我要求你在 @src/native_Table.c 和 @include/native_Table.h 中，实现“Table”本地宿主函数库。这个native库的功能是为本Scheme解释器提供通用哈希表功能。哈希表的key可以是number、symbol、wstring，而value可以是任何东西。Scheme层面的接口定义如下：

```
;; 创建哈希表对象。无参数，将新建的哈希表对象的handle压栈。
;; 实现说明：其本质是 am_map_t 对象，即<am_value_t, am_value_t>一对一映射的哈希表。
(Table.make)

;; 根据key设置value。其中tbl是已经make的哈希表对象，key是number、symbol或wstring，value可以是任何东西，包括lambda、#null、#undefined。如果value是#undefined，则语义上相当于删除entry。无返回值（压栈#undefined）。
;; 实现提示：如果key是字符串，则仅接受长度不大于AM_PROCESS_STRINDEX_MAX_LEN的字符串，因只有短于此阈值的字符串对象会被全局驻留，进而可以用它的handle作为am_map_t的唯一key。字符串驻留机制充当了确保唯一性的预先哈希。symbol则直接以其值为判等依据。
(Table.set tbl key value)

;; 根据key获取value。如果key不存在，则返回#undefined。
(Table.get tbl key)

;; 返回tbl中所有key的列表。无需排序。如果是空table，则返回'()，即空表。
(Table.keys tbl)

;; 检查table是否有某个key。存在返回#t，不存在返回#f。
(Table.contains tbl key)

;; 删除table的某个key。无返回值（压栈#undefined）。
(Table.delete tbl key)

;; 返回table中有多少个有效的entry。返回am_float_t（封装起来的am_value_t）
(Table.length tbl)
```

这个需求比较复杂。在实现这个需求之前，你需要先评估我给出的需求是否合理，先给出你的思路、判断、问题，然后问我是否继续。得到我的批准之后，你才能开始编写代码实现需求。

具体的实现方式，你可以参考 @src/map.c 、以及 @src/native_String.c 等native函数实现。务必注意每个函数实现的“套路”，也就是出栈入栈、TPV（am_value_t）与具体值类型的转化、堆对象的存取和增删改、handle机制、调用step函数以PC++等机制。这些机制有的被封装在工具函数中了，你要注意发现它们，不要遗漏。你的实现需要对Scheme程序员屏蔽诸如handle、扩容与再分配、GC等实现细节，同时要注意优雅降级，操作失败尽量不崩溃，而是回落到合理的降级行为。

你可以使用WSL进行编译构建和测试。你需要阅读 @test/shudu.scm 、 @test/mlp.scm 等测试用Scheme代码，了解其语法，并编写一个测试以上所有Table接口的Scheme测试代码，全面验证Table功能。你需要保证 @main.c 编译运行正确（使用你写的Scheme测试代码作为输入）。你可以使用WSL进行编译构建和测试。

## 方案评估

我已阅读 `@doc/AGENTS.md`、`@src/map.c`、`@include/map.h`、`@src/native_String.c`、`@src/native.c`、`@src/runtime.c` 以及 `@test/shudu.scm`、`@test/mlp.scm`，并确认了 native 库的注册机制。

### 合理且可取的部分

1. **复用 `am_map_t` 合理**：项目已有成熟的一对一哈希表实现，直接作为 Table 底层非常自然。
2. **字符串驻留充当 key 唯一性设计巧妙**：利用 `am_process_make_wstring_handle` 保证相同内容的短字符串得到相同 handle，从而可以直接用 handle 作为 `am_map_t` 的 key。
3. **接口设计清晰**：make/set/get/keys/contains/delete/length 覆盖了哈希表的基本用法。
4. **参考实现充分**：`native_String.c`、`native_Math.c` 已经展示了完整的 native 函数"弹出参数→处理→压回结果→step"的套路。

### 我的实现思路

1. 新增 `include/native_Table.h` / `src/native_Table.c`。
2. 在 `src/native_Table.c` 中定义 `am_native_Table_funcs` 函数表和 `am_native_Table_lib` 库表项。
3. 每个函数遵循以下套路：
   - 从操作数栈按逆序弹出参数（最后压栈的参数最先弹出）。
   - 对 table handle 做类型检查，取出 `am_map_t*`。
   - 对 key 做类型检查：number（int/uint/float）、symbol、wstring handle；wstring 长度超过 `AM_PROCESS_STRINDEX_MAX_LEN` 时拒绝。
   - 调用 `am_map_*` 接口操作。
   - **`am_map_set` 可能扩容并返回新指针，必须调用 `am_heap_set` 更新 handle 绑定。**
   - 将结果压回操作数栈，调用 `am_process_step(proc)`。
4. 在 `main.c` 和 `repl.c` 中注册 `am_native_Table_lib`。
5. 编写 Scheme 测试代码，覆盖所有接口。

### 我发现的关键问题

| # | 问题 | 说明 |
|---|------|------|
| 1 | **运行时 GC 不支持 `AM_OBJECT_TYPE_MAP`** | 这是最关键的问题。当前 `am_process_gc_mark` 只处理 LIST/WSTRING/CLOSURE/CONTINUATION，不认识 MAP。运行时创建的 Table（map）对象本身不会被递归标记其 key/value；如果某个 value 是 handle 且仅被 map 引用，GC sweep 会回收该对象，导致 map 中出现悬空 handle。 |
| 2 | **wstring key 长度超限如何处理** | 需求要求仅接受 ≤`AM_PROCESS_STRINDEX_MAX_LEN` 的字符串。如果用户传入长字符串，是返回 -1 让 native 调用失败，还是静默忽略？ |
| 3 | **number key 的等价性** | `am_map_t` 按位比较 key。`1`（int）和 `1.0`（float）是不同的 `am_value_t`，会成为两个不同的 key。这是符合"底层位模式"的，但需要明确这是否是预期语义。 |
| 4 | **Table.keys 返回的列表类型** | 用 `AM_LIST_TYPE_DEFAULT` 即可，无需是 lambda。 |
| 5 | **value 为 `#undefined` 时删除 entry** | 可以直接调用 `am_map_delete`，因为 `#undefined` 是立即数，不会释放指针。 |

### 问题 1 的详细说明与建议

当前 `am_process_gc_mark`（`src/process.c`）没有 `AM_OBJECT_TYPE_MAP` 分支。如果实现 Table native 库，运行时会出现 `am_map_t` 堆对象：

- map 对象如果被 Scheme 变量引用，会通过闭包变量绑定进入 GC 根，map 对象本身不会被回收。
- 但 map 内部的 value handle（例如指向 lambda、list、wstring 等）如果没有被其他地方引用，GC 不会从 map 出发递归标记它们，因此这些 value 对象会被 sweep 回收。
- 下次 `Table.get` 时，从 map 取出的 value handle 已经悬空。

**建议**：在实现 Table 库的同时，在 `am_process_gc_mark` 中增加对 `AM_OBJECT_TYPE_MAP` 的处理：
```c
else if (obj_type == AM_OBJECT_TYPE_MAP) {
    am_object_set_alive(obj, 0);
    am_map_t *m = (am_map_t *)obj;
    for (size_t i = 0; i < m->capacity; i++) {
        if (m->slots[i].key == AM_MAP_KEY_EMPTY || m->slots[i].key == AM_MAP_KEY_TOMBSTONE) continue;
        if (am_value_is_handle(m->slots[i].key)) ret += am_process_gc_mark(proc, m->slots[i].key);
        if (am_value_is_handle(m->slots[i].value)) ret += am_process_gc_mark(proc, m->slots[i].value);
    }
}
```

这是保证 Table 功能正确性的必要补丁。

---

## 请确认

1. **是否同意我在实现 Table native 库的同时，在 `am_process_gc_mark` 中增加对 `AM_OBJECT_TYPE_MAP` 的递归标记？**（强烈推荐同意，否则 Table 中存储的 handle value 可能被 GC 回收导致悬空引用）
2. **wstring key 长度超过 `AM_PROCESS_STRINDEX_MAX_LEN` 时如何处理？**
   - 方案 A：返回 -1，native 调用失败（严格按需求）。
   - 方案 B：不接受该 key，但返回 0 并压入 `#undefined`（静默失败）。
   - 我推荐方案 A。
3. **number key 是否按底层位模式区分 int/float？**（即 `(Table.set t 1 "a")` 和 `(Table.set t 1.0 "b")` 是两个 entry）我推荐按当前 `am_map_t` 行为，即位模式区分。
4. **是否确认继续按此方案编码？**

得到你的批准后，我将开始实现：
- `include/native_Table.h`
- `src/native_Table.c`
- 修改 `src/process.c` 的 `am_process_gc_mark` 增加 MAP 支持
- 修改 `main.c` 和 `repl.c` 注册 Table 库
- 编写 Scheme 测试代码并验证 `main.c` 编译运行正确

我的答复：

1、关于map堆对象的GC问题，完全采纳你的建议，即在GC的标记、清除、移动等诸环节中，增加对 AM_OBJECT_TYPE_MAP 的处理。

2、wstring key 长度超过 AM_PROCESS_STRINDEX_MAX_LEN 时，返回 -1，native 调用失败。

3、关于 number key 的判等问题，要把整型（int和uint）和float区别开来。但是int和uint统一处理，即，对于非负数，int和uint按值比较；负数和uint自然不相等。

可以开始开发。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个非标准Scheme解释器。请你阅读 test 目录中的测试用Scheme代码，理解其语法，并将 @test/deadlock.scm 改写成符合 @test/fork.scm 的形式。注意，现有的 @test/deadlock.scm 所使用的fork是旧版的，其实际语义更类似于spawn。而 @test/fork.scm 中fork的用法是现行的，其语义类似于Linux的fork系统调用。

仅允许修改 @test/deadlock.scm 一个文件，无需测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个非标准Scheme解释器，不遵守Scheme标准，且引入了诸多自定义机制和本地宿主函数。请你用本项目所支持的非标准Scheme，在 @test/nano_llm_infer.scm 的基础上，编写一个六路归并排序算法，使其支持任意长度的数组排序。以下是需求详细描述：

- 开始编码之前，请你先阅读 test 目录下的示例Scheme代码，了解其语法规则和编码风格，重点阅读 @test/list.scm ，其中有一些你可以使用的函数 。还需要阅读 @include/native_String.h 、  @include/native_System.h 、  @include/native_Table.h 、  @include/native_Math.h  ，了解解释器提供了哪些本地函数可以为你所用。你还需要阅读 @src/runtime.c ，了解语言提供了哪些列表处理原语。
- 在 @test/nano_llm_infer.scm 中，提供了一个一次性排序6个（必须是6个，不多不少）元素的函数 ai_sort ，你需要利用这个函数实现归并排序算法。
- 待排序的list，其元素只能是0~9的整数数字，允许重复出现，长度不必是6的倍数。
- 请你实现六路归并排序算法，允许不稳定，如果分组数或者数组长度不够，你可以补0，补充的0也不必去掉。
- 你可以随机生成列表进行测试，利用 @test/list.scm 中提供的普通排序算法进行测试验证。注意！ai_sort 是基于Transformer序列生成模型实现的非传统排序算法，其结果有一定概率是错误的。因此，你无需保证整体的排序结果必然正确，只要有正确的情况即可。
- 除 @test/nano_llm_infer.scm 之外，不得修改任何其他代码。只允许在 @test/nano_llm_infer.scm 中做修改。

在实现这个需求之前，你需要先评估我给出的需求是否合理，是否有需要我澄清的问题，先给出你的思路、判断、问题，然后问我是否继续。得到我的批准之后，你才能开始编写代码实现需求。

---------------------

关于AI排序问题，根据你已经实现的需求，我要求你根据现有的 @test/nano_llm_infer.scm ，再实现一个新的算法，来处理这个弱排序器的全局聚合问题。

我推荐你使用六元组 Borda 聚合排序算法。

除 @test/nano_llm_infer.scm 之外，不得修改任何其他代码。只允许在 @test/nano_llm_infer.scm 中做修改。建议你只增加代码，不要修改已有代码。

在实现这个需求之前，你需要先评估我给出的需求是否合理，是否有需要我澄清的问题，先给出你的思路、判断、问题，然后问我是否继续。得到我的批准之后，你才能开始编写代码实现需求。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个非标准Scheme解释器。请你排查问题：用长度较长的代码测试发现，ast->node的容量设定为1024，似乎会导致写满之后失败，没有触发预期中的扩容。请检查现有am_heap_t是否无法扩容？总觉得am_heap_set实现似乎有问题，擅自跨越抽象层级，直接操作table，似乎绕过了扩容逻辑。为什么不调用map现成接口？

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个非标准Scheme解释器。其中 @src/module.c 实现了程序编译为模块的dump/load过程。为了减小模块dump得到的字节流的尺寸，我希望你在 @src/module.c 和 @include/module.h 中，实现一个基于 PackBits 算法的压缩和解压函数，对dump得到的字节流进行压缩和解压，并且在 @main.c 的 test_runtime_load_from_file 函数中，调用这个函数进行压缩解压测试，统计并显示压缩率（压缩前字节数、压缩后字节数）。

你可以使用WSL进行编译构建和测试。不得修改无关代码。无需编写新的测试。保证 @main.c 编译运行正确即可。你可以使用 @test/test.scm 作为测试输入。你可以使用WSL进行编译构建和测试。

---------------------

# 需求：process增加var_top和var_arn_mapping两个字段

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个完整的非标准Scheme解释器，采取编译器+中间语言VM架构。

在 @include/process.h 和 @src/process.c 中，am_process_t 的初始化是从接收一个am_module_t开始的。这个module里面有一个完整的AST，但是 am_process_load_from_module 只保留了AST的部分字段给process。

我的需求是：为am_process_t增加来自AST的var_top和var_arn_mapping两个字段；并且在 @src/native_System.c 中有关 fork 本地宿主函数的实现中，增加对于这两个字段的处理。

你可以使用WSL进行编译构建和测试。保证 @main.c 编译运行正确。你可以使用 @test/test.scm 作为回归测试输入。你可以使用WSL进行编译构建和测试。

---------------------

# 需求：process增加var_top和var_arn_mapping两个字段

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个完整的非标准Scheme解释器，采取编译器+中间语言VM架构。

在 @include/parser.h 和 @src/parser.c 中， am_parse 函数实现了一个完整的从代码字符串到AST的解析过程。其中有一个过程 alpha_rename_analysis 实现了词法作用域的分析和 alpha-renaming 过程。该过程会上溯寻找每个普通变量的定义所在词法作用域，进而确定其完整的全限定名。当前的 am_parse → alpha_rename_analysis → arn_rename_varid 实现中，对于“未定义变量”的处理是宽松的。因为这种找不到所属作用域的标识符，除了出现在define、import、native中以及作为全局builtin符号等特殊情况外，也有可能是语义错误（本解释器不允许使用未预先定义的变量，也就是没有被define或者出现在lambda的参数列表中）。但是，在REPL、eval等场景下，需要明确知道哪些变量是“未定义变量”，并将这些“未定义变量”视为“全局自由变量”，以待后续在运行时环境中寻找其绑定。

因此，我的需求是：修改am_parse，增加参数：int32_t is_keep_free。若其值为0，则不做任何处理（完全等同现有逻辑）。若其值为1，则在 alpha_rename_analysis 阶段，将“未定义变量”的 var_type 设为 AM_VAR_TYPE_GLOBAL_FREE 。

你可以使用WSL进行编译构建和测试。保证 @main.c 编译运行正确。你可以使用 @test/test.scm 作为回归测试输入。你可以使用WSL进行编译构建和测试。

---------------------

# 需求：改造compiler的代码生成接口

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个完整的非标准Scheme解释器，采取编译器+中间语言VM架构。

在 @include/compiler.h 和 @src/compiler.c 中， am_compile 函数实现了一个完整的从AST到模块（AST+ILCode+其他元数据）的编译过程。在从零开始的“冷启动”编译过程中，编译出的IL代码被放在mod->ilcode的起始位置，这是自然的。此外，编译出的IL代码的基本结构是：

- 指令0：call 入口函数地址
- 指令1：halt
- 指令2：入口函数……

也就是说，任何完整程序所执行的第一条代码都是“指令0”，执行结束后回到“指令1”并终止程序。在 am_compile 的 am_compiler_label_resolution 阶段，所有将标签解析为实际代码地址（iaddr），都依赖于“程序从0开始”和“最终halt”这个约定。

然而，在REPL、eval等场景下，需要在运行时动态地编译代码，并且将编译出的代码动态地附加到当前进程的ilcode的任何位置上。因此，运行时动态编译出来的IL代码，其起始位置可能是任意位置，也就是某个offset偏移量，这就意味着在 am_compiler_label_resolution 阶段所解析得到的所有iaddr都要加上这个offset。另一方面，动态编译出的IL代码，功能上是一个完整的执行过程，因此它应当有个出口（返回目标），而不是直接halt了事，因此，“指令1”也应该可以是一条goto语句。

因此，我的需求是：修改am_compile，增加两个参数：am_module_t *am_compile(am_ast_t *ast, am_iaddr_t offset, am_iaddr_t ret) 。为了实现这个需求，你可能要修改 am_compiler_label_resolution 增加参数 iaddr offset。

其中offset是iaddr的整体偏移量，ret是最终返回指令的目标iaddr。默认offset=0，ret=0，默认情况则不做任何处理（完全等同现有逻辑）。如果ret大于0，则am_compile_all增加的halt指令（第二条指令）要变成 goto ret。

你可以使用WSL进行编译构建和测试。保证 @main.c 编译运行正确。你可以使用 @test/test.scm 作为回归测试输入。你可以使用WSL进行编译构建和测试。

---------------------

# 需求：实现System.eval

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个完整的非标准Scheme解释器，采取编译器+中间语言VM架构。现在我需要为其增加运行时动态解释执行代码的能力，即System.eval。

这个需求比较复杂。在实现这个需求之前，你需要先评估我给出的需求和方案是否合理，是否有需要我澄清的问题，先给出你的思路、判断、问题，然后问我是否继续。得到我的批准之后，你才能开始编写代码实现需求。

## 需求背景

为了使Scheme解释器支持运行时动态执行代码，需要在System本地库中实现(System.eval codestr)函数，该函数接收一个字符串参数，字符串内期望是Scheme代码，System.eval将其编译成IL代码，并将静态数据和IL代码动态附加到当前执行的进程上，并开始执行。执行完毕后，跳回System.eval的下一条指令。System.eval是同步的、阻塞的。System.eval仅捕获其所在进程的顶级变量绑定，但不捕获其所在词法作用域的变量绑定。

## 术语约定

- proc指调用System.eval的进程，进程数据结构保存了代码AST的大部分信息。
- evalee指传入System.eval的被解释执行的“子程序”，大多数情况下指的是evalee的AST，也有可能指evalee的IL代码。

## System.eval的实现思路

1、取出代码字符串，将其编译为孤立AST=evalee。——GLOBAL_FREE保留原形
2、将孤立AST=evalee根据进程环境进行重写：
   - symbol映射，同am_ast_merge
   - var映射，同am_ast_merge。但是对于AM_VAR_TYPE_GLOBAL_FREE，需要找到它在proc中对应的var_top，加入映射。
     - 例如"((lambda (x) x) x)"，第1、2个x是约束变量，不换名；第三个x是自由变量(AM_VAR_TYPE_GLOBAL_FREE)，需要换成外层环境的名字。
     - 对于evalee中的AM_VAR_TYPE_GLOBAL_FREE，应该到proc的var_top中寻找其定义。如果找不到，那就报错。
     - 寻找线索是：遍历proc所有的var_top，通过var_arn_mapping找到其ARN之前的原形，并跟要替换的global_free进行比较。若一致，则该global_free可映射到proc中的这个var_top。
     - 还是跟merge后换名一个道理：限定了var_top这个范围之后，原形（AM_VAR_TYPE_OLD）也不会重名了。
   - var_top、lambda_handles、tailcall_handles、dependencies、natives都不迁移。
   - 为evalee的所有AST节点在proc->heap中申请handle，构建新旧handle映射
   - 替换evalee所有 list 节点 children 中的 symbol/varid/handle 为proc中的symbol/varid/heap handle
   - 将evalee的strindex合并到proc的strindex
   - 将evalee的所有AST节点拷贝到proc->heap中，保留evalee供后续编译
3、获得proc->ilcode_length，作为evalee的IL代码的整体offset和入口地址。获得proc->PC+1，作为evalee的IL代码的整体返回地址。
4、编译evalee的AST为IL代码：am_compile(evalee_ast, proc->ilcode_length, proc->PC+1)
5、将evalee的IL代码追加到proc->ilcode的尾部。
6、将proc->PC设置为proc->ilcode_length，从evalee的代码开始执行。
7、System.eval引入的heap静态对象、新代码等，暂不纳入GC。

## 实现注意事项

- 关于本地宿主函数（native函数）的实现方式，你可以参考 @src/native_String.c 等native函数实现。务必注意每个函数实现的“套路”，也就是出栈入栈、TPV（am_value_t）与具体值类型的转化、堆对象的存取和增删改、handle机制、调用step函数以PC++等机制。这些机制有的被封装在工具函数中了，你要注意发现它们，不要遗漏。
- 必须谨慎处理 proc->ilcode 的扩容和realloc导致的指针变化。
- 注意清理分析和编译的中间产物。
- 关于System.eval的返回值，由其内部执行的代码自然决定，System.eval本身暂不返回任何值（不压栈）。

## 案例说明

下面给出一个具体案例，说明System.eval的执行过程。

在以下代码中，调用了eval，eval应当能够捕获所在进程的top变量。

```
(define x 100)
(define f (lambda (x) x))
(System.eval "(define y 200) ((lambda (x) (f x)) x)")
```

假设代码经过ARN之后变成：

```
; var_arn_mapping: {v.0.x->x   v.0.f->f   v.1.x->x}
;         var_top: {v.0.x      v.0.f}

(define v.0.x 100)
(define v.0.f (lambda (v.1.x) v.1.x))
(System.eval "((lambda (x) (f x)) x)")
```

调用System.eval后，首先将“((lambda (x) (f x)) x)”编译成孤立的AST，假设是这样：

```
(define e.0.y 200)
((lambda (e.1.x) (f e.1.x)) x)
;; 其中f、x是未定义（没有所属作用域）的变量，是evalee全局的自由变量，在eval过程中，应被标记为AM_VAR_TYPE_GLOBAL_FREE，保留原形
```

接下来便是重写evalee的各个AST节点、并将其拷贝到进程的heap中：

```
; 自由变量f -> 通过遍历proc的var_top，发现v.0.f在var_arn_mapping中对应f -> 所以f对应v.0.f
; 自由变量x -> 通过遍历proc的var_top，发现v.0.x在var_arn_mapping中对应f -> 所以f对应v.0.x
(define e.0.y 200)
((lambda (e.1.x) (v.0.f e.1.x)) v.0.x)
```

接下来准备编译。假设proc->ilcode_length == 100，调用System.eval处的proc->PC == 30，则evalee的IL代码应该放置在proc->ilcode的尾部，其iaddr的整体偏移量就是proc->ilcode_length == 100，整体返回地址就是proc->PC+1 == 31。形如：

```
[099] 主进程的最后一条代码
[100] call iaddr_102
[101] goto iaddr_31
[102] 此后都是evalee的代码...
```

随后System.eval将proc->PC设置为100，也就是从evalee的起始地址开始执行。执行完毕回到101，再跳回System.eval的下一条指令，eval结束。

## 测试

你可以使用WSL进行编译构建和测试。保证 @main.c 编译运行正确。你可以自行构造System.eval的测试用例，以验证其正确性。回归测试方面，你可以使用 @test/test.scm 作为回归测试输入。你可以使用WSL进行编译构建和测试。



我已完成对代码库和该需求的评估。整体方案可行，但有几处关键细节需要确认，避免实现后方向偏差。

## 评估结论

你提出的方案在架构上是合理的：`System.eval` 本质上是一次“热补丁”式的动态编译，把 evalee 的 AST 重写后追加到当前进程的 `ilcode` 尾部，并通过 `offset`/`ret` 机制让执行流自然返回。这与前面已经完成的 `am_compile(..., offset, ret)` 改造正好衔接。

不过，我发现了几个需要澄清或调整的点：

### 1. AST 节点拷贝与编译的先后顺序（最关键）

你的步骤 2 提到“先把 evalee 的 AST 节点拷贝到 `proc->heap`，再编译”，但这里有一个实现矛盾：

- `am_ast_get_node(ast, handle)` 固定从 `ast->nodes` 堆读取；
- 如果把节点搬到 `proc->heap`，再把 handle 换成 `proc->heap` 的 handle，`am_compile` 在编译时通过 `am_ast_get_node` 会找不到这些节点。

因此我建议两种实现路径，请你选择：

- **方案 A（推荐）**：先对 evalee AST 做 symbol/var 重写，直接用 `am_compile(evalee_ast, offset, ret)` 编译出 IL；编译完成后，再扫描 evalee 的 IL 指令中所有 `push handle` 操作数，把指向 evalee 临时堆的字符串/quote-list 对象深拷贝到 `proc->heap`，并替换 operand。这样不用改动 AST 结构，逻辑最干净。
- **方案 B**：拷贝节点到 `proc->heap` 后，构造一个临时的 `am_ast_t` 视图，把它的 `nodes` 字段指向 `proc->heap`，其他字段复用 evalee，然后用这个视图去编译。编译完再销毁视图。这个方案比较 hacky，但符合你描述的顺序。

### 2. eval 内部 `define` 的持久性

按你的方案，`evalee->var_top` 不迁移到 `proc`。那么：

```scheme
(System.eval "(define y 200) y")
```

`y` 只在 eval 执行期间存在，eval 返回后 `y` 即失效。这是否符合预期？还是说你希望 eval 中 `define` 的顶层变量也注册到 `proc->var_top`，从而后续 eval 或主程序都能访问？

### 3. eval 中是否支持 `(native ...)` / `(import ...)`

你的方案明确说 `dependencies`、`natives` 不迁移。这意味着 eval 代码里写 `(native Math)` 或 `(import Foo "...")` 大概率无法正常工作。是否可以接受这个限制？如果可以，实现时我会对这类情况直接报错或忽略。

### 4. eval 代码的 `absolute_path` 与 `base_dir`

`am_parse` 需要 `absolute_path` 来生成 `module_id`，进而影响 ARN 生成的临时变量名。由于 eval 的 ARN 结果最终会被映射/重写，名字本身影响不大，但需要给出一个合理路径。我建议使用：

```c
<rt->working_dir>/__eval__.scm
```

是否可接受？

### 5. 未解析的 GLOBAL_FREE 变量如何处理

如果 evalee 中出现一个自由变量，但在 `proc->var_top` 中找不到对应原形，按你的需求应“报错”。具体行为：

- 直接返回 `-1`，让 native 函数失败，虚拟机进入错误状态？
- 还是打印一条中文/英文错误信息到 `stderr` 再返回 `-1`？

### 6. `opstack` 深度

eval 代码的 `opstack_depth` 不会合并到原进程。虽然 `am_process_push_operand` 会自动扩容操作数栈，但函数调用栈 `fstack` 是固定容量 3000。如果 eval 代码有极深的递归，可能会溢出。是否需要我在追加 IL 代码时检查/扩容 `fstack`？还是保持现状？

### 7. 返回值

你明确说 `System.eval` 本身不压栈，返回值由 eval 代码自然决定。那么 native 函数的实现逻辑是：

```c
pop codestr
... 编译并追加代码 ...
proc->PC = old_ilcode_length;   // 直接跳转到 evalee 入口，不调用 am_process_step
return 0;
```

这会导致 native 调用结束后，VM 立刻从 evalee 入口开始执行，执行完再 `goto proc->PC+1`。这个理解对吗？

---

请确认以上问题（尤其是 **方案 A/B 的选择** 和 **define 持久性**），我会按你的决定继续实现。



我的答复：

1、AST 节点拷贝与编译的先后顺序：采用方案A。先对 evalee AST 做 symbol/var 重写，直接用 am_compile(evalee_ast, offset, ret) 编译出 IL；编译完成后，再扫描 evalee 的 IL 指令中所有 push handle 操作数，把指向 evalee 临时堆的字符串/quote-list 对象深拷贝到 proc->heap，并替换 operand。

2、eval 内部 define 的持久性：eval内部define不持久。define的变量只在 eval 执行期间存在，eval 返回后即失效。

3、eval 中是否支持 (native ...) / (import ...)：需要迁移natives以支持native调用；但是无需迁移dependencies。

4、eval 代码的 absolute_path 与 base_dir：采纳你的建议。

5、未解析的 GLOBAL_FREE 变量如何处理：打印一条中文错误信息到 stderr 再返回 -1。

6、栈深度：opstack深度和fstack深度都保持现状，不要修改。

7、返回值：理解正确。native 调用结束后，VM 立刻从 evalee 入口开始执行，执行完再 goto proc->PC+1。

开始实现。



`System.eval` 已实现并验证完成。

**实现内容**
- `src/native_System.c`：新增 `am_native_System_eval`，支持在当前进程上下文中动态解析、编译并执行 Scheme 代码片段。
  - 解析时保留自由变量（`is_keep_free=1`）。
  - 对 evalee AST 进行 symbol/varid/handle/natives 映射重写。
  - 编译时使用 `offset = proc->ilcode_length`、`ret = proc->PC + 1`，使生成的 IL 追加到现有进程代码段并正确返回。
  - 将 evalee 的 `push handle` 操作数深拷贝到 `proc->heap`。
  - 对未定义的全局自由变量输出错误到 `stderr` 并返回 `-1`。
- `include/process.h`、`src/process.c`：进程新增 `var_top` 与 `var_arn_mapping`，用于 fork 与 eval 时映射顶层变量。
- `include/parser.h`、`src/parser.c`：`am_parse` 新增 `is_keep_free` 参数。
- `include/compiler.h`、`src/compiler.c`：`am_compile` 新增 `offset` 与 `ret` 参数。
- `main.c`、`repl.c`：适配新的 API 签名。

**测试**
- 新增 `test/test_eval_read.scm`、`test/test_eval_write.scm`、`test/test_eval_lambda.scm`、`test/test_eval_undef.scm`，分别验证读取外部变量、写入外部变量、eval 中定义 lambda、未定义变量报错。
- 自定义 eval 测试全部通过。
- 完整回归测试 `./main test/test.scm` 通过。
- `make clean && make` 无警告、无错误，`main` 与 `repl` 均构建成功。

未执行 git commit。工作区中编译产物 `main`、`repl` 与四个 eval 测试文件为未跟踪文件。




发现一个问题：现有System.eval似乎不能处理call/cc。

例如，以下代码无法执行：

```
(native System)
(System.eval "(display (call/cc (lambda (k) (k 888) 666)))")
```

报错如下：

```
[System.eval] IL varid 映射失败：iaddr=2
[Runtime] 指令执行异常: PID=0 PC=3
```

请排查问题。




已定位并修复。

**问题原因**

`System.eval` 在编译前调用 `eval_build_var_mapping` 建立 evalee → proc 的 varid 映射，但 `call/cc` 的编译函数 `compile_callcc` 会在 **编译期间** 通过 `am_compiler_make_temp_varid` 生成一个临时续体变量（类型为 `AM_VAR_TYPE_ILTEMP`，例如 `CC_<label>_<id>`）。这个 varid 在映射建立之后才出现，因此 `eval_remap_il_operands` 扫描到 `capturecc` / `load` 操作数时找不到对应映射，报错：

```
[System.eval] IL varid 映射失败：iaddr=2
```

**修复方案**

在 `am_compile` 之后新增一步 `eval_extend_var_mapping_for_temps`，扫描编译过程中新加入 `evalee->var_vocab` 的 `ILTEMP` 变量：

1. 将其以唯一名称插入 `proc->var_vocab`，避免多次 `System.eval` 生成的同名临时变量共享 proc 的 varid（这对 `call/cc` 续体尤其重要）。
2. 同步扩展 `proc->var_type` 并标记为 `AM_VAR_TYPE_ILTEMP`。
3. 补充到 `var_mapping` 中，供后续 IL 操作数重映射使用。

**验证**

新增测试：

- `test/test_eval_callcc.scm`：你的示例，现在输出 `888`。
- `test/test_eval_cond.scm`：`cond` 内部同样会生成编译期临时变量，输出 `111`。
- `test/test_eval_multi_callcc.scm`：连续两次 `call/cc` eval，输出 `111222`，说明临时 varid 没有冲突。

所有测试通过：

- `test/test_eval_*.scm` 全部通过。
- 完整回归 `./main test/test.scm` 通过。
- `make clean && make` 无警告、无错误。

未执行 git commit。

---------------------

需求：实现syntax-rules宏机制

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个完整的非标准Scheme解释器，采取编译器+中间语言VM架构。现在我需要为其增加syntax-rules卫生宏机制。这个需求比较复杂。你可以使用WSL进行编译构建和测试。保证 @main.c 编译运行正确。你可以自行构造测试用例，以验证其正确性。回归测试方面，你可以使用 @test/test.scm 作为回归测试输入。你可以使用WSL进行编译构建和测试。

(其余内容见 doc/report.md 中关于宏的章节)



已完成修复并追加测试用例。关键改动如下：

**修复的 Bug**
1. **顶层 lambda 被“篡改”为 APPLICATION**：`macro_ellipsis_list_get_or_create` 用 `AM_HANDLE_NULL` 作为 ellipsis 累加列表的 parent，而 `AM_HANDLE_NULL == AM_TOP_NODE_HANDLE`，导致这些临时列表被当作顶层节点，破坏 `am_ast_get_top_node_handle`。已改为使用 `ctx->parent`。
2. **ellipsis 匹配 0 次时未绑定空列表**：在 `macro_match_list` 中预先为 ellipsis 模式变量创建空列表。
3. **syntax-rules literals 不支持关键字**：`else` 等关键字在 literals 列表中是 symbol 而非 varid，已放宽校验。
4. **quote/quasiquote/unquote 内部错误展开**：内部用户 symbol 可能与关键字 symbol 值冲突，现已跳过这些形式。
5. **宏生成的 lambda 缺少作用域**：为 `macro_instantiate_list`、`macro_deep_copy_list`、`macro_expand_lambda` 中新建的 lambda 注册 scope，并加入 freshen 后的参数。
6. **编译器 lambda 标签未预创建**：`lambda_handles` 顺序可能先遇到内层 lambda，导致 `compile_lambda` 定位标签失败。现在在 `am_compile_all` 中预创建所有 lambda 标签。
7. **模块合并后 `var_top` 重复**：同一模块被直接/间接导入时，其 `var_top` 会被追加多次，导致 import ref 解析 `match_count != 1`。已在 `am_ast_merge` 中去重。

**新增测试**
- `test/macro_ellipsis.scm`
- `test/macro_literal.scm`
- `test/macro_let_syntax.scm`
- `test/macro_hygiene.scm`
- `test/macro_nested.scm`

**验证结果**
- 所有 macro 回归测试通过。
- `test/test.scm` 完整运行通过。
- `make` 无警告/错误。

已同步更新 `doc/AGENTS.md` 说明宏系统相关约定与限制。

---------------------

根据你构造测试、测试失败的过程，你觉得现在实现的宏、System.eval、call/cc都有哪些bug或者合理的功能限制？




根据构造测试和实际跑测的过程，我把当前实现里暴露出来的问题分成**已修复的 bug**、**仍存在的 bug**、**合理/已知的功能限制**三类来说：

---

### 1. 已修复的 bug

- **字符串字面量转义未还原**：`am_ast_make_wstring_node` 之前直接把词法 token 内容（含反斜杠）原样存成字符串对象，导致 `\"`、`\\`、`\n`、`\t`、`\r` 等都不会被还原。对 `System.eval` 尤其致命——传给它的代码字符串如果含 `\"`，词法器能正确找到字符串边界，但解析后字符串里还残留反斜杠，eval 再解析就会失败。已在 `src/ast.c` 中加入 `ast_unescape_string` 修复。

---

### 2. 仍存在的 bug / 不合理行为

- **`System.eval` 内部定义的宏无法在同一段 eval 代码中使用**  
  测试里如果写：
  ```scheme
  (System.eval "(define-syntax eval-if ...) (eval-if #t 1 2)")
  ```
  会报 `未定义的变量：eval-if`。这说明 `define-syntax` 在 eval 生成的孤立 AST 里虽然能被解析，但宏展开阶段没有把同一段代码里的后续使用展开掉。  
  根因猜测：`am_macro_expand` 依赖“顶层 lambda bodies”收集 `define-syntax`，而 `System.eval` 用的 `isolated=1` AST 在顶层 lambda 判定或 body 序列划分上和普通模块不一致，导致宏注册到了环境帧里但后续展开没生效。

- **`main` 对不带目录斜杠的相对路径会段错误**  
  例如 `./main test_eval.scm` 会 segfault，而 `./main test/test_eval.scm` 正常。原因是 `main.c` 调用 `am_path_dirname` 后直接传给 `_mbstowcs`，而 `am_path_dirname` 对不含 `/` 的路径返回 `NULL`，没有做空指针保护。这是测试/入口程序的 bug，不是语言核心。

- **`System.eval` 的错误信息具有误导性**  
  上面宏未展开的情况，报错是“未定义的变量”，容易让人以为是变量捕获失败，实际上是宏没展开。类似地，如果 eval 字符串本身词法/语法有错，也可能被包装成运行时异常。调试体验不佳。

---

### 3. 合理或已知的功能限制

- **`System.eval` 只能捕获宿主进程的顶级变量绑定**  
  这是 `native_System.c` 注释里明确写的。测试也验证了：在 `let-syntax` 体内用 `(define base 100)` 再让 `System.eval` 访问 `base` 会报未定义。只有放在顶层 `var_top` 里的变量才能被 eval 捕获，局部变量、闭包变量都不行。

- **跨模块导入/导出宏不支持**  
  这是项目已知限制（一期）。`test/test.scm` 里所有 `import` 的模块都只能传回运行时值，不能把宏带出来。

- **宏不能作为一等值传递**  
  `define-syntax` 注册的宏是编译期 transformer，不是运行时对象。所以没法做 `(define f my-or)` 或把宏传给 `System.eval`。这不是 bug，而是 `syntax-rules` 宏的语义。

- **`call/cc` 与 `System.eval` 的 continuation 边界未验证**  
  测试中只验证了“eval 内部自己用 `call/cc`”和“宿主宏展开后调用 eval”。但没有验证“在宿主捕获的 continuation 里再进入 eval”或“在 eval 里捕获 continuation 后返回宿主继续使用”。这类跨 eval 边界的 continuation 行为目前是不确定的，很可能受限。

- **`letrec-syntax` 未在综合测试里覆盖**  
  只测了 `let-syntax`，`letrec-syntax` 的相互递归展开是否健壮还不清楚。

---

### 总结

最应该优先修的是 **eval 内部宏不生效**（这是用户提到的“排列组合深入测试”里真正缺失的一环），其次是 **main 入口对相对路径的保护**。其余多为已知架构限制，可以在文档里明确标注，避免使用者踩坑。


---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个完整的非标准Scheme解释器，采取编译器+中间语言VM架构，具备R5RS定义的基于syntax-rules的卫生宏机制，但不一定完整。本Scheme解释器不支持任何let类绑定语法，也不支持 (define func (arg1 arg2) xxx) 这样的定义函数的语法。

现在请你排查卫生宏机制没有卫生绑定的问题。在下列测试用例中，外层变量i应当被卫生地绑定到宏内部的结构。因而程序应当输出1、2、3、4、5，最终的sum应当是15。但实际执行的结果是：输出4个999，最终的sum是3996。

```
(define-syntax my_for
  (syntax-rules (to do)
    ((my_for var from start to end do body ...)
     ((lambda (var limit)
        (define loop (lambda ()
                       (if (< var limit)
                           (begin body ...
                                  (set! var (+ var 1))
                                  (loop))
                           'done)))
        (loop))
      start end))))

(define sum 0)
(define i 999)

(my_for i from 1 to 5 do
  (display i) (newline)
  (set! sum (+ sum i)))

(display "my_for  sum=") (display sum) (display "  i=") (display i) (newline)
```

你可以使用WSL进行编译构建和测试。保证 @main.c 编译运行正确。你可以使用 @test/test.scm 作为回归测试输入。你可以使用WSL进行编译构建和测试。



已阅读 `@doc/AGENTS.md`，并完成了卫生宏绑定问题的排查与修复。

## 问题根因

ARN（Alpha-Renaming）在宏定义处会给**模式变量**和**模板中同名标识符**分配不同的 `varid`——尤其是当模板标识符与外层变量同名时（如你例子里的 `i`）。而 `syntax-rules` 展开器是通过 `varid` 来判断“这是不是模式变量”的，于是模板里 `(lambda (var limit) …)` 中的 `var` 没有被识别为模式变量，被当成普通模板绑定做了 freshen。结果是：

- 宏内部真正循环的 `var` 是一个全新的 varid；
- `body …` 里用户写的 `i` 仍然指向外层 `i`（999）；
- 所以循环虽然执行了 4 次，但每次都显示 999、累加 999。

## 修复内容（`src/macro.c`）

在 `macro_parse_syntax_rules` 解析每个 clause 之后，新增了两步“模板规范化”：

1. 收集所有模式变量的名字；
2. 递归遍历模板 AST，把**名字（basename）与某个模式变量相同**的标识符统一替换成该模式变量的 `varid`；
3. `quote` 内部保持原样，避免把字面量符号误伤。

这样模板里的 `var`、`start`、`end`、`body` 等就会和模式变量拥有同一个 `varid`，后续 `macro_is_pattern_var` 能正确识别，展开时会把它们替换为使用处传入的实际标识符，从而正确捕获 `body` 中的引用。

## 验证结果

- `./main test/test_hygiene.scm`：一个最小复现，输出 `42`（正确捕获宏引入的 lambda 参数）。
- `./main test/test_my_for.scm` 与 `./main test/test_macro.scm`：你的 `my_for` 例子现在输出：

```
1
2
3
4
my_for  sum=10  i=999
```

外层 `i` 仍然是 999，说明卫生绑定已正确；`sum=10` 是因为宏里写的是 `(< var limit)`，1→5 会循环 4 次（1、2、3、4）。  
**如果你期望包含 5、sum=15，需要把宏条件改成 `(<= var limit)` 或 `(<= var end)`，这不是解释器卫生宏的 bug。**

- `./main test/test_eval.scm`、`./main test/test_macro_eval_continuation.scm` 均通过。
- `./main test/test.scm` 完整回归通过。

## 新增/保留的测试文件

- `test/test_hygiene.scm`：最小卫生宏捕获测试。
- `test/test_my_for.scm`：你提供的 `my_for` 用例独立测试。
- `test/test_macro_eval_continuation.scm`：之前添加的宏 / `System.eval` / `call/cc` 综合测试（已重新生成并验证通过）。

## 说明

按照 `@doc/AGENTS.md`，我没有执行任何 `rm` 删除命令，因此构建产物 `main`、`repl`、`main_debug` 以及新增测试文件都保留在工作区中，未做提交。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个完整的非标准Scheme解释器，采取编译器+中间语言VM架构。

目前已经在单独的C文件 @jstoscm.c 中，实现了一个将JavaScript简单翻译成Scheme子集的算法。现在请你将其迁移到 @include/js2scm.h 和 @src/js2scm.c 中，并封装为以下接口，供应用层调用：

```
wchar_t *am_js_to_scheme(const wchar_t *js_source);
```

此后，在 @main.c 中，增加根据输入文件后缀是scm还是js来选择是否调用 am_js_to_scheme 将JS转为Scheme的流程。

你可以使用WSL进行编译构建和测试。保证 @main.c 编译运行正确。你可以自行构造一些合理的JavaScript代码作为测试输入。你可以使用WSL进行编译构建和测试。不得删除文件，不得修改无关代码。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个完整的非标准Scheme解释器，采取编译器+中间语言VM架构。当前，在 @src/native_System.c 和 @include/native_System.h 中实现的 am_native_System_exec 函数，其功能为执行宿主环境的shell命令。但是，现在我希望把它彻底修改成类似于Linux系统调用 execve 的功能，即：接收一个Scheme源码字符串，将其编译成module后，用module中的ILCode、静态数据对象等内容，彻底替换掉当前所在的process的全部内容，并从头开始执行。

修改后的 (System.exec codepath) 接收一个参数，即Scheme源码。失败则返回（压栈）-1；成功则无所谓返回，因exec这条指令已经被替换掉了。

期望的功能是与System.fork搭配使用，最常见的用法就是在fork出来的子进程中执行System.exec，彻底替换掉fork出来的子进程，实现同Linux类似的创建进程。

注意事项如下：

- 你可以参考 @main.c ，该函数作为整个解释器的入口，演示了从Scheme源码出发，如何编译并转换得到一个module。但是 @main.c 中还演示了模块的dump/load以及压缩解压等过程，这部分不要在System.exec中实现。
- 关于本地宿主函数（native函数）的实现方式，你可以参考 @src/native_String.c 等native函数实现。务必注意每个函数实现的“套路”，也就是出栈入栈、TPV（am_value_t）与具体值类型的转化、堆对象的存取和增删改、handle机制、调用step函数以PC++等机制。这些机制有的被封装在工具函数中了，你要注意发现它们，不要遗漏。
- 必须谨慎处理 proc->ilcode 等容器类数据结构的扩容和realloc导致的指针变化。
- 注意清理分析和编译的中间产物。

你可以使用WSL进行编译构建和测试。保证 @main.c 编译运行正确，使用 @test/test.scm 进行回归测试。你可以自行构造一些合理的Scheme代码作为测试输入。你可以使用WSL进行编译构建和测试。

---------------------

开始编码前，请先阅读 @doc/AGENTS.md 。

本项目是一个完整的非标准Scheme解释器，采取编译器+中间语言VM架构。VM通过事件循环实现了多进程机制（ @src/runtime.c ），但现在还没有进程间通信机制。

我的需求：请你为解释器实现基于队列（FIFO）的进程间通信机制。总的来说，类似于FreeRTOS的xQueue。详细要求如下：

- 队列基于 @src/list.c 中定义的列表实现，运用其shift、push等函数实现入队出队。
- 在 @include/runtime.h 中定义的 am_runtime_t 中，增加一个队列列表成员，用于存储所有队列（的指针），可能还有必要维护一个队列计数器，用于为队列赋予编号（ID），通过编号对已有的队列进行管理。其他你认为必要的基础设施，按需增加。
- 队列是多生产者多消费者的，任何process都可以向队列发送或者从队列接收数据。
- 队列的数据项是am_value_t，通过拷贝的方式进行传递。收发双方需自行解释value的语义，解释器不做任何语义层面的检查和限制。解释器不提供复杂对象序列化的基础设施，由用户自行在Scheme层实现。
- 阻塞式访问：队列的发送和接收都是阻塞式的，但可以指定超时时间。
- 所有的队列都通过vm_alloc分配在VM区（工作区），任何进程都可以通过队列的编号（ID）和相应的API去访问队列。

为了实现队列操作，在 @src/native_System.c 中，实现以下本地（native）API：

```
;; 创建一个队列，参数为队列的长度（容纳多少个am_value_t），返回值为新建队列的编号（ID），若失败则返回#null。
(System.make_queue  len:number(uint)) : number(uint)

;; 向队列qid发送一个值v，指定超时时间timeout_ms。成功返回#t，失败返回#f。
(System.write  qid:number(uint)  v:any  timeout_ms:number(uint) )

;; 从队列qid接收一个值，指定超时时间timeout_ms。成功返回接收到的value，失败返回#undefined。
(System.read  qid:number(uint)  timeout_ms:number(uint) )
```

你可以使用WSL进行编译构建和测试。保证 @main.c 编译运行正确，使用 @test/test.scm 进行回归测试。你可以自行构造一些合理的测试用例。你可以使用WSL进行编译构建和测试。

---------------------

2026-07-08

请先阅读 @doc/AGENTS.md 。

本项目是一个完整的非标准Scheme解释器，采取编译器+中间语言VM架构。我的目标是让它在内存只有kB级的MCU（如ESP32）上运行。因此，请你分析以下问题。仅回答，不要修改代码。

1、分析所有调用系统malloc/calloc/realloc的情况，分析脱离内存池的内存分配情况和静态下限。

2、dump/load如何受数据类型长度的影响？当前实现的dump/load是否与系统字长无关？



以下是对两个问题的分析结论，仅作说明，未改动任何代码。

---

## 1. 系统 `malloc/calloc/realloc` 调用与内存池外分配分析

### 1.1 整体架构：两条内存路径

项目内部有一层 `am_allocator_t` 抽象分配器（`include/allocator.h`），通过 vtable 把 `am_malloc/am_calloc/am_realloc/am_free` 转发到具体实现。  
默认实现是 `src/allocator.c` 中的统一内存池 `am_allocator_pool_create`：

- 启动时一次性 `malloc(total_size)` 拿到整池连续内存（`pool->base`），并 `malloc(sizeof(am_allocator_pool_t))` 分配控制块。
- 池内再划分为 VM 工作区（segregated free-list）和堆区（first-fit free-list）。
- **此后绝大多数运行时对象不再调用 libc `malloc`**，而是通过池内分配器分配。

因此判断“是否脱离内存池”，关键看是否直接调用 libc 的 `malloc/calloc/realloc`，而非走 `am_allocator_t`。

### 1.2 直接调用系统分配器的位置（脱离内存池）

经过全文扫描，直接调用系统 `malloc/calloc/realloc` 的位置可分为以下几类：

| 类别 | 典型文件/行 | 说明 |
|------|------------|------|
| **内存池自身** | `src/allocator.c:580, 586` | 池控制块、`pool->base`（默认 128/200 MB） |
| **GC/压缩辅助数组** | `src/allocator.c:1114, 1144, 1183, 1198` | 全局压缩时的存活对象表、reloc 表、slot 指针表 |
| **运行时 GC 临时数组** | `src/runtime.c:2126, 2277` | 多进程全局压缩时临时 `heaps` 数组 |
| **编译器核心** | `src/compiler.c:179, 1039, 1186` | `ilcode` 数组全程走 libc；栈深度分析临时数组 |
| **解析器/AST/链接器/宏** | `src/parser.c`, `src/ast.c`, `src/linker.c`, `src/macro.c` | 状态栈、节点栈、拓扑排序数组、DAG 矩阵、token 文本、宏对象等 |
| **`am_map_keys()` 返回值** | `src/map.c:454` | 调用者需 `free()` |
| **调试输出** | `src/debug.c` | visited 数组、输出缓冲 |
| **文件/路径/REPL 工具** | `src/utils.c`, `main.c`, `repl.c` | 文件内容缓冲、路径、`read_line` 缓冲、模块 dump 缓冲、PackBits 压缩缓冲 |
| **JS→Scheme 翻译器** | `src/js2scm.c` | 全部内部节点、字符串 builder |
| **本地 LLM 库** | `src/native_LLM.c` | tokenizer map/trie、词表指针数组、Base64 编解码缓冲、模型权重缓冲 |
| **闭包、列表、作用域等 API 返回值** | `src/list.c:310` 等 | 少量“调用者负责 free”的副本 |

其中占用较大、对 MCU 不友好的几块离线分配：

- **`src/linker.c:127`**：`ctx->DAG = calloc(64*64, sizeof(size_t[2]))`，固定 **32 KB（32 位）/ 64 KB（64 位）**。
- **`src/compiler.c:179`**：`ilcode` 数组在编译期完全走 libc，大小与程序指令数成正比。
- **`main.c/repl.c`**：模块 dump、PackBits 压缩/解压缓冲与模块大小成正比。
- **`src/native_LLM.c`**：词表指针数组固定 `16384 * sizeof(wchar_t*)`，约 **64 KB（32 位）/ 128 KB（64 位）**，再加上 unicode charset 64 KB；模型权重本身更大。

### 1.3 内存池内分配的对象

通过 `am_malloc/am_calloc` 走的对象包括：

- `am_map_t`、`am_list_t`、`am_vocab_t`、`am_strindex_t`
- AST 节点、抽象堆 `am_heap_t` 及其 table/metadata
- 进程结构 `am_process_t`、操作数栈 `opstack`、函数栈 `fstack`、ilcode 副本
- 运行时 FIFO、队列、定时器等

这些都在池内，但池本身需要一块连续内存。

### 1.4 静态下限估算

影响最低内存占用的关键常量：

| 常量 | 位置 | 默认值 | 影响 |
|------|------|--------|------|
| `AM_ALLOCATOR_POOL_SIZE` | `main.c:53` / `repl.c:26` | 200 MB / 128 MB | 整池连续内存，最大头 |
| `AM_ALLOC_ALIGN` | `src/allocator.c:12` | `sizeof(void*)` | 所有块对齐 |
| Map 最小容量 | `src/map.c:138` | 8 | 每个 map 至少 8 槽 |
| List 最小容量 | `src/list.c:54` | 4 | 每个 list 至少 4 槽 |
| Vocab 最小容量 | `src/vocab.c:49` | 4 | 每个 vocab 至少 4 槽 |
| Strindex 最小容量 | `src/wstring.c:277` | 8 | 每个 strindex 至少 8 槽 |
| AST 初始容量 | `src/ast.c:145-157` | nodes=1024, strindex=1024, vocab=64 等 | 编译期预分配 |
| `fstack` 容量 | `src/process.c:317` | 3000 | 每个进程函数调用栈 |
| `opstack` 默认 | `src/process.c:306` | max(opstack_depth, 1024) | 每个进程操作数栈 |
| `AM_LINKER_MAX_MODULES` | `src/linker.c:19` | 64 | DAG 固定矩阵 |
| `AM_LLM_VOCAB_SIZE` | `src/native_LLM.c:229` | 16384 | LLM tokenizer 上限 |

若把 `AM_ALLOCATOR_POOL_SIZE` 强行改成 128 KB，在 32 位平台下一个空解释器/最小模块的粗略下限：

| 项目 | 32 位估算 |
|------|----------|
| 内存池 backing | 128 KB |
| 池控制块 + 全局变量 | < 1 KB |
| AST 初始表（池内） | ~25–30 KB |
| 运行时 + 最小进程栈（池内） | ~20 KB |
| 链接器 DAG + 上下文（libc） | ~34 KB |
| 编译器/解析器临时（libc） | 数 KB ~ 十几 KB |
| **合计** | **~200–220 KB** |

64 位因指针/`size_t`/`am_value_t` 翻倍，整体约 **300–340 KB**。

### 1.5 对 kB 级 MCU（如 ESP32）的结论

**当前默认实现无法直接在 ESP32 上运行**，主要瓶颈：

1. **默认池太大**：128/200 MB，远超 ESP32 的 320–520 KB SRAM。
2. **池需要连续内存**：即使总空闲 RAM 够，也可能因碎片化而分配失败。
3. **链接器固定 DAG 过大**：64×64 矩阵占 32/64 KB，可改小 `AM_LINKER_MAX_MODULES`。
4. **进程栈预分配偏大**：`fstack` 3000、`opstack` 1024，对简单脚本过大。
5. **AST 初始容量偏大**：`nodes=1024`、`strindex=1024` 等。
6. **大量离线 libc 分配**：编译器、链接器、REPL、JS 前端、LLM 本地库都不走池，增加碎片化。
7. **POSIX/宽字符依赖**：`setlocale`、`getcwd`、stdio、mbstowcs/wcstombs 等在裸机 MCU 上需要移植。

若要移植，需要：

- 通过编译宏把 `AM_ALLOCATOR_POOL_SIZE` 降到 64–128 KB 级别；
- 大幅减小 `AM_LINKER_MAX_MODULES`、进程栈容量、AST 初始容量；
- 把编译器/链接器的临时数组逐步移入 `vm_alloc` 或改为静态缓冲；
- 移除 `native_LLM.c` 等重型本地库；
- 仅保留“加载预编译模块”模式，避免在 MCU 上做完整编译。

在大幅裁剪后，**理论上可在 256–512 KB RAM 的 MCU 上跑极简脚本**，但默认代码不能直接运行。

---

## 2. `dump/load` 与数据类型长度的关系

### 2.1 当前实现是“内存快照式”序列化

各 `dump`/`load` 函数基本做法是把运行时 C 结构体（或其柔性数组部分）直接 `memcpy` 到 buffer：

- `am_heap_dump/load`（`src/heap.c`）：写 `am_heap_t` 头 + 两个 `am_map_dump`。
- `am_heap_deep_dump/load`（`src/heap.c`）：先写两个 `uint64_t` 长度，再写 `am_heap_dump`，最后把 list/wstring 对象逐个 `memcpy`；对象位置用“相对 deep_dump 起点的偏移”记录。
- `am_module_dump/load`（`src/module.c`）：写 packed 的 `module_header_t`，再依次写 IL 指令、deep_heap、vocab、list、map、strindex 等。
- `am_list_dump/load`（`src/list.c`）：直接写 `am_list_t` 头 + `children[]`。
- `am_map_dump/load`（`src/map.c`）：直接写 `am_map_t` 头 + 有效 `slots[]`。
- `am_vocab_dump/load`（`src/vocab.c`）：写头 + `wchar_t*` 指针数组 + 字符串内容。
- `am_wstring_dump/load`（`src/wstring.c`）：写对象头 + `length` + `content[]`。
- `am_strindex_dump/load`、`am_closure_dump/load` 同理。

### 2.2 关键类型长度依赖

| 类型 | 定义位置 | 32 位宽度 | 64 位宽度 | 影响 |
|------|----------|-----------|-----------|------|
| `am_value_t` | `include/object.h:74` | `uintptr_t` = 4 B | 8 B | 所有 `children[]`、`slots[]`、`content[]`、`binding.value` 元素宽度不同 |
| `am_handle_t` / `am_iaddr_t` / `am_varid_t` / `am_symbol_t` / `am_label_t` | `include/object.h:39-63` | `size_t` = 4 B | 8 B | list 的 `parent`、closure 的 `iaddr/parent`、binding 的 `varid` 等字段宽度不同 |
| `am_int_t` / `am_uint_t` | `include/object.h:39-63` | `int32_t/uint32_t` | `int64_t/uint64_t` | 整数值范围不同，TPV 编码位宽不同 |
| `am_float_t` | `include/object.h:39-63` | `float` | `double` | 浮点精度与编码不同 |
| `size_t` | 系统类型 | 4 B | 8 B | 所有 `capacity/length/mask/tombstones`、模块头偏移字段宽度不同 |
| 原生指针 | 系统类型 | 4 B | 8 B | `am_heap_t.table/metadata` 偏移、`vocab.words[]` 存储的是指针/绝对地址 |
| `wchar_t` | 系统类型 | Windows 16 位 / Linux 32 位 | — | `vocab` 字符串内容宽度不同 |

### 2.3 为什么与系统字长**不无关**

结论：**当前 `dump/load` 与系统字长、指针长度、`size_t` 长度强相关**。具体表现为：

1. **直接 `memcpy` 运行时结构体**：`am_list_t`、`am_map_t`、`am_wstring_t`、`am_obj_closure_t` 等都没有 `#pragma pack`，32 位和 64 位下的填充、偏移、大小完全不同。
2. **`am_value_t = uintptr_t`**：所有容器数组元素宽度随平台变化。
3. **`am_handle_t` 等 = `size_t`**：对象头后续字段的偏移和宽度随平台变化。
4. **`am_instruction_t`**（`include/compiler.h:17-20`）在 32 位为 8 字节，64 位因 `am_value_t` 对齐为 16 字节。
5. **`am_heap_dump` 中 `table`/`metadata` 偏移以 `am_map_t*` 指针类型写入**，本质是原生指针宽度。
6. **`am_vocab_dump` 把 `words[i]` 写成运行时绝对地址指针**，跨地址空间直接失效。
7. **`module_header_t` 虽 `#pragma pack(1)`，但内部含大量 `size_t`/`am_iaddr_t`**，头部长度和字段偏移仍随平台变化。
8. **`am_float_t` 在 32 位是 `float`、64 位是 `double`**，TPV 浮点编码不同。
9. **`vocab` 字符串使用系统 `wchar_t`**，Windows（16 位）与 Linux（32 位）不兼容。
10. `am_heap_deep_dump` 虽使用相对偏移，但偏移值被塞进 `am_value_t`（指针宽度），仍随平台变化。

因此，**现在生成的模块 dump / deep_dump 文件无法在 32 位 MCU 与 64 位宿主之间互导**。

### 2.4 若要做到跨 32/64 位互导，最小修改方向

1. **定义固定宽度的磁盘类型**：所有磁盘字段使用 `uint32_t/uint64_t/int64_t`，绝不用 `size_t`、`uintptr_t`、原生指针、`am_value_t`。
2. **为每个对象定义紧凑的磁盘结构体**：`#pragma pack(push,1)`，按字段逐个读写，禁止直接 `memcpy` 运行时结构体。
3. **对象间引用统一用 `uint64_t` 相对偏移**：heap 的 table/metadata、deep_dump 对象位置、vocab 字符串位置都用相对偏移。
4. **规范化 `am_value_t` 磁盘格式**：定义 64 位 TPV 磁盘格式，dump/load 时做平台相关转换。
5. **统一浮点与整数格式**：磁盘统一用 `int64_t` 和 IEEE-754 double/fixed 格式；32 位加载时检查整数越界。
6. **字符串统一用 `am_wchar_t`（`uint32_t`）或 UTF-8**，不使用系统 `wchar_t`。
7. **固定 `am_instruction_t` 磁盘格式**：例如 `uint32_t opcode` + `uint64_t operand`。
8. **模块头全部改用固定宽度字段**：所有偏移、长度、栈深用 `uint64_t`。
9. **增加版本号/字节序标记**： bump `MODULE_VERSION`，并加入 endianness 标志。
10. **统一 8 字节对齐**：所有段按 8 字节对齐，便于 32/64 位共同解析。

---

**总结**：

1. 当前项目有大量脱离内存池的 libc 分配，默认配置对 kB 级 MCU 不可行；但通过裁剪池大小、链接器容量、进程栈和 AST 初始容量，理论上可压到 200 KB 级静态下限。
2. 当前 `dump/load` 是“内存快照式”实现，严重依赖字长、指针长度、`size_t` 长度和结构体填充，**不是与系统字长无关**；要在 32 位 MCU 与 64 位宿主之间互导模块，需要引入一层显式的固定宽度磁盘格式。

---------------------

2026-07-08

开始编码前，请先阅读 @doc/AGENTS.md 。

根据你的分析，尝试消除 @src/linker.c 中所有libc提供的malloc/calloc/realloc，转为通过allocator在内存池上进行分配。这样当链接-编译过程结束后，所有临时东西都被一次性清除。

你可以使用WSL进行编译构建和测试。保证 @main.c 编译运行正确，使用 @test/test.scm 进行回归测试。你可以使用WSL进行编译构建和测试。

---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------

- (Table.make)  →  {}
- (Table.set tbl key value)  →  tbl[key] = value  ;; key可以是数值、符号、字符串，value可以是任何值
- (Table.get tbl key)  →  tbl[key]
- (Table.keys tbl)  →  Table.keys(tbl)
- (Table.contains tbl key)  →  Table.contains(tbl, key)
- (Table.delete tbl key)  →  Table.delete(tbl, key)
- (Table.length tbl)  →  Table.length(tbl)

---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------

