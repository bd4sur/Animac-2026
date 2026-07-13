(define k #f)
(define out '())
(define invoked #f)
(dynamic-wind
  (lambda () (set! out (cons 'before out)))
  (lambda ()
    (call/cc (lambda (c) (set! k c)))
    (set! out (cons 'thunk out)))
  (lambda () (set! out (cons 'after out))))

;; 第一次正常进入/退出后，out 为 (thunk after before)
(if (not invoked)
    (begin
      (set! invoked #t)
      (k #f)))
;; 再次通过 continuation 进入/退出后，out 应为 (thunk after before after before)

(display "call/cc: out = ")
(display out)
(newline)

;; 检查前 6 个元素
(define ok #t)
(if (not (equal? (get_item out 0) 'after))  (set! ok #f))
(if (not (equal? (get_item out 1) 'thunk))  (set! ok #f))
(if (not (equal? (get_item out 2) 'before)) (set! ok #f))
(if (not (equal? (get_item out 3) 'after))  (set! ok #f))
(if (not (equal? (get_item out 4) 'thunk))  (set! ok #f))
(if (not (equal? (get_item out 5) 'before)) (set! ok #f))
(if ok
    (display "✅ PASS call/cc\n")
    (display "❌ FAIL call/cc\n"))
