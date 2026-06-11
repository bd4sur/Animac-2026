;; 词法作用域

(define free 100)
(define foo (lambda () `(,free))) ; 准引用列表也是词法作用域的
(define bar (lambda (free) (foo)))
(bar 200) ; 输出(100)，而不是(200)


;; 函数作为第一等公民
(import List "list.scm") ; 引入列表操作高阶函数
(native Math) ; 声明使用数学本地库
(List.reduce '(1 2 3 4 5 6 7 8 9 10) + 0) ; 55
(List.map '(-2 -1 0 1 2) Math.abs)        ; (2 1 0 1 2)
(List.filter '(0 1 2 3)
             (lambda (x) (= 0 (% x 2))))  ; (0 2)


;; 循环结构（注意：循环体内并非如同JavaScript的块作用域）
(define sum 0)
(define i 1)
(while (<= i 100) {
  (set! sum (+ sum i))
  (set! i (+ i 1))
})
(display sum)  ;; 输出5050


;; Quine（自己输出自己的程序）
((lambda (x) (cons x (cons (cons quote (cons x '())) '()))) (quote (lambda (x) (cons x (cons (cons quote (cons x '())) '())))))


;; 续体和`call/cc`
;; Yin-yang puzzle
;; see https://en.wikipedia.org/wiki/Call-with-current-continuation
(((lambda (x) (begin (display "@") x)) (call/cc (lambda (k) k)))
 ((lambda (x) (begin (display "*") x)) (call/cc (lambda (k) k))))
; Output @*@**@***@**** ...
