(native Math)
(native System)
(native String)

(import Church "church_encoding.scm")
(import Coroutine "coroutine.scm")
(import Calendar "calendar.scm")

(import Fac "factorial.scm")
(import FFT "fft.scm")
(import Generator "generator.scm")
(import Interpreter "interpreter.scm")
(import List "list.scm")
(import MoB "man_or_boy.scm")
(import MLP "mlp.scm")


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


(Calendar.run)
(Church.run)
(Fac.run)
(FFT.run)
(Generator.run)
(Interpreter.run)
(List.run)
(MoB.run)
(MLP.run)

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


(Coroutine.run)
