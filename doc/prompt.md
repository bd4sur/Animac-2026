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

链接器全局上下文数据结构定义：

typedef struct am_linker_ctx_t {
    size_t module_counter;
    am_vocab_t *all_module_path; // mod_index -> module_path
    am_ast_t *ALLAST; // mod_index->ast
    size_t DAG[][2]; // 邻接关系列表 importer_index -> importee_index
    size_t sorted_ast_index[];
    size_t edge_num;
} am_linker_ctx_t;

### 链接过程1：递归解析所有依赖模块

从起始代码开始，解析为AST，读取其所有导入文件，并逐个解析为AST，递归读取并解析，过程中完成：1收集AST；2构建DAG。伪代码如下：

```
void import_analyse(am_linker_ctx_t *ctx, wchar_t *importee_path, size_t importer_index) {
    size_t current_module_index = vocab_find_index(ctx->all_module_path, importee_path);
    if (current_module_index == SIZE_MAX) {
        current_module_index = ctx->module_counter;
        ctx->all_module_path[current_module_index] = importee_path;
        wchar_t *code = read_from_file(importee_path);
        am_ast_t *current_ast = am_parser(ctx->alloc, code, importee_path);
        ctx->ALLAST[current_module_index] = current_ast;
        foreach (path of current_ast->dependencies) {
            import_analyse(ctx, path, current_module_index);
        }
        ctx->module_counter++;
    }
    ctx->DAG[ctx->edge_num] = {importer_index, current_module_index});
    ctx->edge_num++;
}
```

### 链接过程2：对引用关系做拓扑排序，并按照线性顺序逐步融合

// 对DAG进行拓扑排序的工具函数
size_t *topo_sort(size_t DAG[][2], size_t edge_num); // 输出拓扑排序后的mod_index列表，由调用者负责free；成环则返回NULL

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
// 逐个吃掉依赖模块
for (size_t i = 1; i < ctx->module_counter; i++) {
    size_t importee_index = ctx->sorted_ast_index[i];
    am_ast_merge(global_ast, ALLAST[importee_index]);
}
```






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





### 链接过程3：对所有模块做外部引用解析

```
// 对所有模块做外部引用解析
am_linker_import_ref_resolution(ctx);
// 功能说明：对AST中的import_ref类型的变量进行解析，替换为所在模块中的变量全限定名
// 实现说明：
int32_t am_linker_import_ref_resolution(am_linker_ctx_t *ctx) {
    for (size_t i = 0; i < ctx->module_counter; i++) {
        
    }
}
```

am_linker_import_ref_resolution(ctx, importer_index)解引用算法：
- 首先要知道，外部引用解析发生在AST大一统之后，所有的名字都是全局唯一无歧义的。
- 全量遍历整个合并后的AST，在children中寻找IMPORT_REF类型的变量。
- 从最后一个点将IMPORT_REF分割为 prefix=importer_id.alias 和 suffix。
- 在全局dependencies中，查询 prefix=importer_id.alias 对应的 importee_path ，转为模块ID：importee_id。
- 在var_top中查找IMPORT_REF在importee中的变量名：匹配以下模式的变量：importee_id.[top_lambda_node_of_importee].suffix。说明：尽管中间的[top_lambda_node_of_importee]不知道，但是，通过importee_id+顶级变量+suffix三个信息，已经能够在var_top中唯一定位出一个变量。如有任何异常（找不到等）则报错。
- 用新找到的top_var变量，替换掉旧的IMPORT_REF类型的child。
- 对所有IMPORT_REF变量的child重复以上过程，直至全部遍历完毕。


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


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------


---------------------

