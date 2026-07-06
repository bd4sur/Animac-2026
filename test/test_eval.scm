(native System)

(define x 100)
(define y 0)

(display "====== System.eval 测试 ======") (newline)

(display "预期输出：100\n实际输出：")
(System.eval "(display x)")
(newline)

(display "预期输出：100\n实际输出：")
(System.eval "(set! y x)")
(display y)
(newline)

(display "预期输出：1000\n实际输出：")
(System.eval "(display ((lambda (y) (* x y)) 10))")
(newline)

(display "预期输出：666\n实际输出：")
(System.eval "(display (cond ((> x 2) 666) (else 888)))")
(newline)

(display "预期输出：666\n实际输出：")
(System.eval "(display (call/cc (lambda (k) (k 666) 888)))")
(newline)

(display "预期输出：999\n实际输出：")
(System.eval "(display (call/cc (lambda (k) (k 999) 666)))")
(newline)

(display "((lambda (x) (cons x (cons (cons quote (cons x '())) '()))) (quote (lambda (x) (cons x (cons (cons quote (cons x '())) '())))))\n实际输出：")
(System.eval "(display ((lambda (x) (cons x (cons (cons quote (cons x '())) '()))) (quote (lambda (x) (cons x (cons (cons quote (cons x '())) '()))))))")
(newline)

(display "预期输出：报错（变量z未定义）\n实际输出：")
(System.eval "(display z)")
(newline)
