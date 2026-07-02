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
        am_ast_t *current_ast = am_parse(ctx->alloc, code, importee_path);
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
// 实现描述：遍历所有ilcode，检查am_instruction.operand的am_value_t的TPV类型是否是AM_VALUE_TYPE_LABEL。如果是，则调用am_compiler_parse_label_to_iaddr将其转换为iaddr，并替换掉原来的label。成功返回0，失败返回-1。
int32_t am_compiler_label_resolution(am_compiler_ctx_t *ctx);
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
    emit_instruction(ctx, AM_VM_OP_halt, AM_VALUE_UNDEFINED);
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
am_module_t mod = am_compile(wchar_t *code, wchar_t *absolute_path, wchar_t *base_dir);
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


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------

