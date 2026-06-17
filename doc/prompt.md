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

本项目是一个C语言编写的Scheme解释器，尚未完成。请你通读项目C语言代码，并在 @parser.c 中完成以下需求。

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

