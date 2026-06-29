(native Math)
(native System)
(native String)

(import Coroutine "coroutine.scm")
(import List "list.scm")
(import Lib "lib.scm")

(List.run) (newline)










;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


;; 递归实现快速傅里叶变换
;; 2023-08

;; 把序列按照奇偶分成两部分
(define sep
  (lambda (x)
    (define even
      (lambda (input even_items odd_items)
        (if (null? input)
            `(,even_items ,odd_items)
            (odd (cdr input) (List.append (car input) even_items) odd_items))))
    (define odd
      (lambda (input even_items odd_items)
        (if (null? input)
            `(,even_items ,odd_items)
            (even (cdr input) even_items (List.append (car input) odd_items)))))
    (even x '() '())))

(define complex_mul
  (lambda (x y)
    (define a (car x)) (define b (car (cdr x)))
    (define c (car y)) (define d (car (cdr y)))
    `(,(- (* a c) (* b d)) ,(+ (* b c) (* a d)))))

(define complex_add (lambda (x y) `(,(+ (car x) (car y)) ,(+ (car (cdr x)) (car (cdr y))))))

(define complex_sub (lambda (x y) `(,(- (car x) (car y)) ,(- (car (cdr x)) (car (cdr y))))))

(define list_pointwise
  (lambda (op x y)
    (if (or (null? x) (null? y))
        '()
        (cons (op (car x) (car y))
              (list_pointwise op (cdr x) (cdr y))))))

(define W_nk
  (lambda (N k)
    `(,(Math.cos (/ (* -2 (* (Math.PI) k)) N))
      ,(Math.sin (/ (* -2 (* (Math.PI) k)) N)))))

(define twiddle_factors
  (lambda (N iter)
    (if (== iter (/ N 2)) ;; 只取前一半
        '()
        (cons (W_nk N iter)
              (twiddle_factors N (+ iter 1))))))

(define fft
  (lambda (input N)
    (if (== N 1)
        input
        {
          (define s (sep input))
          (define even_dft (fft (car s)       (/ N 2)))
          (define odd_dft  (fft (car (cdr s)) (/ N 2)))
          (define tf (twiddle_factors N 0))
          (List.concat (list_pointwise complex_add even_dft (list_pointwise complex_mul odd_dft tf))
                       (list_pointwise complex_sub even_dft (list_pointwise complex_mul odd_dft tf)))
        })))

(define ifft
  (lambda (input N)
    ;; 复数列表逐个取共轭
    (define cv_conj
      (lambda (cv)
        (List.map cv (lambda (c) `(,(car c) ,(- 0 (car (cdr c))))))))
    (List.map (cv_conj (fft (cv_conj input) N))
              (lambda (x) `(,(/ (car x) N) ,(/ (car (cdr x)) N))))))




(display "快速傅里叶变换：用于测试数学本地库和列表操作")(newline)
(display "FFT期望结果：((8 1) (0 1) (0 1) (0 1) (0 1) (0 1) (0 1) (0 1))")(newline)
(define N 8)
(define x '((1 1) (1 0) (1 0) (1 0) (1 0) (1 0) (1 0) (1 0)))
(define xx (fft x N))
(define x2 (ifft xx N))
(display "FFT实际结果：")
(display xx)
(newline)
(display "IFFT期望结果：((1 1) (1 0) (1 0) (1 0) (1 0) (1 0) (1 0) (1 0))")(newline)
(display "IFFT实际结果：")
(display x2)
(newline)
(newline)


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;



;; 尾递归优化演示
(define sum
  (lambda (n s)
    (if (== n 0)
        s
        (sum (- n 1) (+ n s)))))
;; 开尾递归优化
(display "尾（递归）调用优化测试") (newline)
(display "Sum(1~100000) = ")
(display (sum 100000 0))
(newline)
(newline)


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(Lib.Calendar 2026 6)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(Lib.MoB 10)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define filter
  (lambda (f lst)
    (if (null? lst)
        '()
        (if (f (car lst))
            (cons (car lst) (filter f (cdr lst)))
            (filter f (cdr lst))))))

(define concat
  (lambda (a b)
    (if (null? a)
        b
        (cons (car a) (concat (cdr a) b)))))

(define partition
  (lambda (op pivot array)
    (filter (lambda (x) (if (op x pivot) #t #f)) array)))

(define quicksort
  (lambda (array)
    (define pivot #f)
    (if (or (null? array) (null? (cdr array)))
        array
        {
          (set! pivot (car array))
          (concat (quicksort (partition < pivot array))
                  (concat (partition == pivot array)
                          (quicksort (partition > pivot array))))
        }
    )
))


(display "快速排序：测试验证列表操作、if、and/or等特殊结构")(newline)
(display "期望结果：(-3 -3 -2 -1 0 1 2 3 4 5 5 6 6 6 7 8 9)")(newline)
(display "实际结果：")
(display (quicksort '(6 -3 5 9 -2 6 1 7 -3 5 3 0 4 -1 6 8 2)))
(newline)
(newline)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(display "Quine测试：")(newline)
(display "预期输出：")
(display "((lambda (x) (cons x (cons (cons quote (cons x '())) '()))) (quote (lambda (x) (cons x (cons (cons quote (cons x '())) '())))))")
(newline)
(display "实际输出：")
(display
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ((lambda (x) (cons x (cons (cons quote (cons x '())) '()))) (quote (lambda (x) (cons x (cons (cons quote (cons x '())) '())))))
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
)
(newline)
(newline)


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;



(define fac_cps
(lambda (cont)
  (cont (lambda (n)
          (lambda (k)
            ((lambda (cont)
               ((lambda (cont)
                  ((lambda (cont) (cont (lambda (x y) (lambda (k) (k (== x y)))))) ; 内置相等判断
                   (lambda (node0)
                     ((node0 0 n)
                      (lambda (res) (cont res))))))
                (lambda (p_res)
                  (if p_res
                      ((lambda (cont) (cont 1))
                       cont)
                      ((lambda (cont)
                         ; 以下仅仅是对每个AST节点进行简单的遍历CPST/重命名,并未体现求值顺序，可以理解成并行的
                         ((lambda (cont) (cont (lambda (x y) (lambda (k) (k (* x y)))))) (lambda (node0) ; 内置乘法
                         ( fac_cps                                                       (lambda (node1) ; 递归调用(重命名后的)
                         ((lambda (cont) (cont (lambda (x y) (lambda (k) (k (- x y)))))) (lambda (node2) ; 内置减法
                         ; 从这里开始体现求值顺序,几乎等于是 A-Normal Form
                         ((node2 n 1)    (lambda (res2)
                         ((node1 res2)   (lambda (res1)
                         ((node0 n res1) (lambda (res)
                         ; 最后执行总的continuation
                         ( cont res))))))))))))))
                       cont)))))
             (lambda (m) (k m))))))))


(define fac-count 0)
(define clo-count 0)
(define fac
  (lambda (n cont) (begin
    (set! fac-count (+ fac-count 1))
    (if (== n 0)
        (cont 1)
        (fac (- n 1)
             (lambda (res) (begin
               (set! clo-count (+ clo-count 1))
               (cont (* res n)))))))))


(define sum_iter
  (lambda (n init)
    (if (== n 0)
        init
        (sum_iter (- n 1) (+ n init)))))



(display "阶乘测试①：真·CPS阶乘")(newline)
(display "期望结果：3628800")(newline)
(display "实际结果：")
(((fac_cps (lambda (x) x)) 10) (lambda (x) (display x)))
(newline)
(newline)

(display "阶乘测试②：CPS和set!的结合")(newline)
(display "5!（期望120）=")
(display (fac 5 (lambda (x) x)))
(newline)
(display "闭包调用次数（期望5）=")
(display clo-count)
(newline)
(display "阶乘递归调用次数（期望6）=")
(display fac-count)
(newline)
(newline)

(display "尾调用优化测试：大量的尾递归调用")(newline)
(display "期望结果：5000050000")(newline)
(display "实际结果：")
(display (sum_iter 100000 0))
(newline)
(newline)

(display "快速求幂算法：测试cond语句")(newline)
(display "期望结果：1073741824")(newline)
(display "实际结果：")
(define power
  (lambda (base exp init)
    (cond ((== exp 0) init)
          ((== 0 (mod exp 2)) (power (* base base) (/ exp 2) init))
          (else (power base (- exp 1) (* base init))))))
(display (power 2 30 1))
(newline)
(newline)


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;



;; 生成器示例
;; 用于演示一等Continuation
;; 说明：本解释器暂时没有将顶级作用域特殊看待，导致捕获Continuation时会同时捕获到后续的generator调用，形成递归。因此引入了判断，使得演示程序能够在10轮递归之内结束。
;; 预期结果：输出1~10

(define count 0)
(define generator #f)
(define g
  (lambda ()
    ((lambda (init)
      (call/cc (lambda (Kont)
                 (set! generator Kont)))
      (set! init (+ init 1))
      (set! count init)
      init) 0)))


(display "测试：使用call/cc模拟其他高级语言的生成器。")(newline)
(display "此用例用来测试call/cc。")(newline)
(display "期望结果：1 2 3 4 5 6 7 8 9 10")(newline)
(display "实际结果：")
(display (g))
(display " ")
(if (>= count 10)
    (newline)
    (display (generator 666)))
(newline)
(newline)



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;



;; The Little Schemer 书中给出的Scheme解释器

(define build
  (lambda (s1 s2)
    (cons s1 (cons s2 '()))))

(define first
  (lambda (list-pair)
    (car list-pair)))

(define second
  (lambda (list-pair)
    (car (cdr list-pair))))

(define third
  (lambda (list-pair)
    (car (cdr (cdr list-pair)))))

(define new-entry build)

(define lookup-in-entry-help
  (lambda (name names values entry-f)
    (cond ((null? names) (entry-f name))
          ((eq? (car names) name) (car values))
          (else (lookup-in-entry-help name (cdr names) (cdr values) entry-f)))))

(define lookup-in-entry
  (lambda (name entry entry-f)
    (lookup-in-entry-help name (first entry) (second entry) entry-f)))

(define extend-table cons)

(define lookup-in-table
  (lambda (name table table-f)
    (cond ((null? table) (table-f name))
          (else (lookup-in-entry name
                                 (car table)
                                 (lambda (n)
                                   (lookup-in-table n
                                                    (cdr table)
                                                    table-f)))))))

(define expression-to-action
  (lambda (e)
    (cond ((atom? e) (atom-to-action e))
          (else (list-to-action e)))))

(define atom-to-action
  (lambda (e)
    (cond ((number? e) *const)
          ((eq? e #t) *const)
          ((eq? e #f) *const)
          ((eq? e 'cons) *const)
          ((eq? e 'car) *const)
          ((eq? e 'cdr) *const)
          ((eq? e 'null?) *const)
          ((eq? e 'eq?) *const)
          ((eq? e 'atom?) *const)
          ((eq? e 'zero?) *const)
          ((eq? e 'add1) *const)
          ((eq? e 'sub1) *const)
          ((eq? e '+) *const)
          ((eq? e '-) *const)
          ((eq? e '*) *const)
          ((eq? e '/) *const)
          ((eq? e '=) *const)
          ((eq? e 'begin) *const)
          ((eq? e 'display) *const)
          ((eq? e 'number?) *const)
          (else *identifier))))

(define list-to-action
  (lambda (e)
    (cond ((atom? (car e))
           (cond ((eq? (car e) 'quote)  *quote)
                 ((eq? (car e) 'lambda) *lambda)
                 ((eq? (car e) 'cond)   *cond)
                 (else *application)))
          (else *application))))

(define meaning
  (lambda (e table)
    ((expression-to-action e) e table)))

(define value
  (lambda (e)
    (meaning e '())))

(define *const
  (lambda (e table)
    (cond ((number? e) e)
          ((eq? e #t) #t)
          ((eq? e #f) #f)
          (else (build 'primitive e)))))

(define text-of second)

(define *quote
  (lambda (e table)
    (text-of e)))

(define initial-table (lambda (name) (car '())))

(define *identifier
  (lambda (e table)
    (lookup-in-table e table initial-table)))

(define *lambda
  (lambda (e table)
    (build 'non-primitive (cons table (cdr e)))))

(define table-of first)
(define formals-of second)
(define body-of third)

(define else?
  (lambda (x)
    (cond ((atom? x) (eq? x 'else))
          (else #f))))

(define question-of first)
(define answer-of second)

(define evcon
  (lambda (lines table)
    (cond ((else? (question-of (car lines))) (meaning (answer-of (car lines)) table))
          ((meaning (question-of (car lines)) table) (meaning (answer-of (car lines)) table))
          (else (evcon (cdr lines) table)))))

(define cond-lines-of cdr)

(define *cond
  (lambda (e table)
    (evcon (cond-lines-of e) table)))

(define evlis
  (lambda (args table)
    (cond ((null? args) '())
          (else (cons (meaning (car args) table)
                      (evlis (cdr args) table))))))

(define function-of car)
(define arguments-of cdr)

(define *application
  (lambda (e table)
    (apply (meaning (function-of e) table)
           (evlis (arguments-of e) table))))

(define primitive?
  (lambda (l)
    (eq? (first l) 'primitive)))

(define non-primitive?
  (lambda (l)
    (eq? (first l) 'non-primitive)))

(define apply
  (lambda (fun vals)
    (cond ((primitive? fun)
           (apply-primitive (second fun) vals))
          ((non-primitive? fun)
           (apply-closure (second fun) vals))
          (else (display "Error occured in 'apply'!")))))

(define isAtom
  (lambda (x)
    (cond ((atom? x) #t)
          ((null? x) #f)
          ((eq? (car x) 'primitive) #t)
          ((eq? (car x) 'non-primitive) #t)
          (else #f))))

(define apply-primitive
  (lambda (name vals)
    (cond ((eq? name 'cons)  (cons (first vals) (second vals)))
          ((eq? name 'car)   (car (first vals)))
          ((eq? name 'cdr)   (cdr (first vals)))
          ((eq? name 'null?) (null? (first vals)))
          ((eq? name 'eq?)   (eq? (first vals) (second vals)))
          ((eq? name 'atom?) (isAtom (first vals)))
          ((eq? name 'zero?) (== (first vals) 0))
          ((eq? name 'add1)  (+ 1 (first vals)))
          ((eq? name 'sub1)  (- (first vals) 1))
          ((eq? name '+)     (+ (first vals) (second vals)))
          ((eq? name '-)     (- (first vals) (second vals)))
          ((eq? name '*)     (* (first vals) (second vals)))
          ((eq? name '/)     (/ (first vals) (second vals)))
          ((eq? name '=)     (== (first vals) (second vals)))
          ((eq? name 'begin)   (second vals))
          ((eq? name 'display) (display (first vals)))
          ((eq? name 'number?) (number? (first vals)))
          (else (display "Unknown primitive function.")))))

(define apply-closure
  (lambda (closure vals)
    (meaning (body-of closure)
             (extend-table (new-entry (formals-of closure) vals)
                           (table-of closure)))))



(display "The Little Schemer 书中给出的Scheme解释器：")(newline)
(display "期望输出：31") (newline)
(display "((lambda (x) (add1 x)) 30)=")
(display (value '((lambda (x) (add1 x)) 30))) (newline)
(display "10!（期望输出3628000）=")
(display (value '(((lambda (S)
                    ((lambda (x) (S (lambda (y) ((x x) y))))
                      (lambda (x) (S (lambda (y) ((x x) y))))))
                  (lambda (f)
                    (lambda (n)
                      (cond ((= n 0) 1)
                            (else (* n (f (- n 1)))))))) 10)))
(newline)
(newline)


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


;; 丘奇编码
;; https://en.wikipedia.org/wiki/Church_encoding

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 布尔值
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define SHOWBOOL
  (lambda (b)
    (b #t #f)))

(define TRUE  (lambda (x y) x))
(define FALSE (lambda (x y) y))

(define NOT
  (lambda (bool)
    (bool FALSE TRUE)))

(define AND
  (lambda (boolx booly)
    (boolx booly boolx)))

(define OR
  (lambda (boolx booly)
    (boolx boolx booly)))

(define IS_ZERO
  (lambda (n)
    (n (lambda (x) FALSE) TRUE)))

(define IF
  (lambda (p x y)
    (p x y)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 自然数
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define SHOWNUM
  (lambda (n)
    (n (lambda (x) (+ x 1)) 0)))

(define NUM_TO_LAMBDA
  (lambda (number)
    (if (== number 0)
        NUM_0
        (INC (NUM_TO_LAMBDA (- number 1))))))

(define NUM_0 (lambda (f a) a))

(define NUM_1 (lambda (f a) (f a)))

(define INC
  (lambda (n)
    (lambda (f a)
      (f (n f a)))))

(define ADD
  (lambda (m n)
    (m INC n)))

;Curried-ADD - for function MUL
(define ADD_c
  (lambda (m)
    (lambda (n)
      (m INC n))))

(define MUL
  (lambda (m n)
    (n (ADD_c m) NUM_0)))

;Curried-MUL - for function POW
(define MUL_c
  (lambda (m)
    (lambda (n)
      (n (ADD_c m) NUM_0))))

(define POW
  (lambda (m n)
    (n (MUL_c m) NUM_1)))

;some paticular numbers
(define NUM_2 (lambda (f a) (f (f a))))
(define NUM_3 (lambda (f a) (f (f (f a)))))
(define NUM_4 (lambda (f a) (f (f (f (f a))))))
(define NUM_5 (lambda (f a) (f (f (f (f (f a)))))))
(define NUM_6 (lambda (f a) (f (f (f (f (f (f a))))))))
(define NUM_7 (lambda (f a) (f (f (f (f (f (f (f a)))))))))
(define NUM_8 (lambda (f a) (f (f (f (f (f (f (f (f a))))))))))
(define NUM_9 (lambda (f a) (f (f (f (f (f (f (f (f (f a)))))))))))
(define NUM_10 (lambda (f a) (f (f (f (f (f (f (f (f (f (f a))))))))))))
(define NUM_11 (lambda (f a) (f (f (f (f (f (f (f (f (f (f (f a)))))))))))))
(define NUM_12 (lambda (f a) (f (f (f (f (f (f (f (f (f (f (f (f a))))))))))))))
(define NUM_13 (lambda (f a) (f (f (f (f (f (f (f (f (f (f (f (f (f a)))))))))))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 有序对和减法
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define PAIR
  (lambda (x y)
    (lambda (f)
      (f x y))))

(define LEFT
  (lambda (pair)
    (pair TRUE)))

(define RIGHT
  (lambda (pair)
    (pair FALSE)))

;substraction
(define SLIDE
  (lambda (pair)
    (PAIR (RIGHT pair) (INC (RIGHT pair)))))

(define DEC
  (lambda (n)
    (LEFT (n SLIDE (PAIR NUM_0 NUM_0)))))

(define SUB
  (lambda (m n)
    (n DEC m)))

;comparation
(define IS_LE
  (lambda (num1 num2)
    (IS_ZERO (SUB num1 num2))))

(define IS_EQUAL
  (lambda (num1 num2)
    (AND (IS_LE num1 num2) (IS_LE num2 num1))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Z组合子（Y组合子的应用序求值版本）
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;Y-Combinator
;注意：目标函数应使用单参形式
(define Y
  (lambda (S)
    ( (lambda (x) (S (lambda (y) ((x x) y))))
      (lambda (x) (S (lambda (y) ((x x) y)))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 整数（暂时没有用）
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define INT
  (lambda (neg pos)
    (PAIR neg pos)))

(define INT_ZREO
  (PAIR NUM_0 NUM_0))

(define INT_IS_ZERO
  (lambda (int)
    (AND (IS_ZERO (LEFT  int))
         (IS_ZERO (RIGHT int)))))

;整数标准化，也就是简化成至少一边为0的形式，这样就可以实现绝对值函数和符号函数了
(define INT_NORMALIZE
  (lambda (int)
    (IF (IS_LE (LEFT int) (RIGHT int))
        (INT NUM_0 (SUB (RIGHT int) (LEFT int)))
        (INT (SUB (LEFT int) (RIGHT int)) NUM_0))))

(define INT_ABS
  (lambda (int)
    (IF (IS_ZERO (LEFT (INT_NORMALIZE int)))
        (RIGHT (INT_NORMALIZE int))
        (LEFT  (INT_NORMALIZE int)))))

;TRUE +; FALSE -
(define INT_SGN
  (lambda (int)
    (IS_ZERO (LEFT (INT_NORMALIZE int)))))

(define SHOWINT
  (lambda (int)
    (if (SHOWBOOL (INT_SGN int))
        {(display "+") (SHOWNUM (INT_ABS int))}
        {(display "-") (SHOWNUM (INT_ABS int))})))

(define INT_ADD
  (lambda (i j)
    (INT (ADD (LEFT  i) (LEFT  j))
         (ADD (RIGHT i) (RIGHT j)))))

(define INT_MUL
  (lambda (i j)
    (INT (ADD (MUL (LEFT i) (LEFT j)) (MUL (RIGHT i) (RIGHT j)))
         (ADD (MUL (LEFT i) (RIGHT j)) (MUL (RIGHT i) (LEFT j))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 列表（二叉树）
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; TODO NOTE 【注意】这里体现了Animac的define与标准define的不同之处。Aurora的被define的项是不求值的，因此如果想使用它的值，就需要把它封装成一个thunk，使用的时候调用之。
(define NULL_LIST
  (lambda ()
    (PAIR TRUE TRUE)))

(define IS_NULLLIST
  (lambda (list)
    (LEFT list)))

(define CONS
  (lambda (e l)
    (PAIR FALSE (PAIR e l))))

(define CAR
  (lambda (list)
    (LEFT (RIGHT list))))

(define CDR
  (lambda (list)
    (RIGHT (RIGHT list))))

(define COUNT
  (lambda (l)
    ((Y (lambda (f)
          (lambda (list)
            (IF (NOT (IS_NULLLIST list))
                (lambda (x y) ((INC (f (CDR list)))
                               x
                               y))
                NUM_0))))
     l)))

(define SHOWLIST
  (lambda (list)
    (if (SHOWBOOL (IS_NULLLIST list))
        (display "N)")
        {
            (display (SHOWNUM (CAR list)))
            ;(display ",")
            (SHOWLIST (CDR list))
        }
    )))

;闭区间
;注意Currying
(define RANGE
  (lambda (m n)
    (((Y (lambda (f)
          (lambda (a)
            (lambda (b)
            (IF (IS_LE a b)
                (lambda (z) ((CONS a ((f (INC a)) b))
                               z ))
                (NULL_LIST)
            )))))m)n)))

;高阶函数Fold和Map
(define FOLD
  (lambda (list init func)
    ((((Y (lambda (f)
          (lambda (l)
            (lambda (i)
              (lambda (g)
                (IF (IS_NULLLIST l)
                    i
                    (lambda (x y) (
                      (g (CAR l) (((f (CDR l)) i) g))
                      x y))
                ))))))list)init)func)))

(define MAP
  (lambda (list func)
    (((Y (lambda (f)
           (lambda (l)
             (lambda (g)
               (IF (IS_NULLLIST l)
                   (NULL_LIST)
                   (lambda (x) ((CONS (g (CAR l)) ((f (CDR l)) g)) x))
                )))))list)func)))

; 投影函数（常用）
(define PROJ
  (lambda (list index)
    ((((Y (lambda (f)
            (lambda (l)
              (lambda (i)
                (lambda (j)
                  (IF (IS_EQUAL i j)
                      (CAR l)
                      (lambda (x y) ((((f (CDR l)) i) (INC j)) x y))
                   ))))))list)index)NUM_0)))

(define run
  (lambda () {

    (display "Church编码：测试Scheme语言核心")
    (newline)

    (display "6!=")
    (display
    (SHOWNUM 
    ((Y (lambda (f)
        (lambda (n)
          (IF (IS_EQUAL n NUM_0)
              NUM_1
              (lambda (x y) ((MUL n (f (DEC n)))
                              x
                              y))
          ))))
    NUM_6)))
    (newline)

    (display "Count(1,2,3,3,3)=")
    (display (SHOWNUM (COUNT (CONS NUM_1 (CONS NUM_2 (CONS NUM_3 (CONS NUM_3 (CONS NUM_3 (NULL_LIST)))))))))
    (newline)

    (display "List=(")
    (SHOWLIST (CONS NUM_1 (CONS NUM_2 (CONS NUM_3 (CONS NUM_4 (CONS NUM_5 (NULL_LIST)))))))
    (newline)

    (display "Range(2,7)=(")
    (SHOWLIST (RANGE NUM_2 NUM_7))
    (newline)

    (display "Fold(1:10,0,ADD)=")
    (display (SHOWNUM (FOLD (RANGE NUM_1 NUM_10) NUM_0 ADD)))
    (newline)

    (display "MAP(1:9,0,INC)=(")
    (SHOWLIST (MAP (RANGE NUM_1 NUM_9) INC))
    (newline)

    (display "Proj(2:10,5)=")
    (display (SHOWNUM (PROJ (MAP (RANGE NUM_1 NUM_9) INC) NUM_5)))
    (newline)

    (newline)

  })
)
(run)




;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;




(Coroutine.run)




;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;



;; 异步回调测试：睡眠排序 :-)
;; 2025-07-03
;; 这是一种幽默的排序算法，指的是对列表中每个元素（数值n）创建一个线程，
;; 每个线程延迟n毫秒后将其输出，这样自然就得到了排序好的列表
;; 由于JS时钟并不精确，因此输出结果有随机性，可以多运行几次

(define array '(9 1 8 6 2 7 3 6 0 4 5))
(define sorted '())

(define make_promise
  (lambda (i)
    (System.set_timeout (+ 20 (* 50 (get_item array i))) ;; 因JS时钟分辨率有限，延时至少20ms
      (lambda ()
        (set! sorted (List.append (get_item array i) sorted))))))

(display "排序前：") (display array) (newline)

(define idx 0)
(while (< idx (length array)) {
  (make_promise idx)
  (set! idx (+ idx 1))
})

(System.set_timeout 800 (lambda () (display "排序后：") (display sorted) (newline)))



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


(define pid (System.fork))
(if (== 0 pid) {
  (display "child")
} {
  (System.set_interval 1000 (lambda () (newline) (display "Heartbeat @ ") (display (System.timestamp)) (newline)))
})
