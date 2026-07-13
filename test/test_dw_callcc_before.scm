(native System)

(define out '())
(define mylog (lambda (x) (set! out (cons x out))))
(define k #f)
(define captured #f)
(define invoked #f)

(dynamic-wind
  (lambda ()
    (mylog 'before)
    (if (not captured)
        (call/cc (lambda (c)
                   (set! k c)
                   (set! captured #t)))))
  (lambda ()
    (mylog 'thunk)
    (if captured
        (if (not invoked)
            (begin
              (set! invoked #t)
              (k #f)))))
  (lambda () (mylog 'after)))

(display "before call/cc: out = ")
(display out)
(newline)

(define ok #t)
(if (not (equal? (get_item out 0) 'after))  (set! ok #f))
(if (not (equal? (get_item out 1) 'thunk))  (set! ok #f))
(if (not (equal? (get_item out 2) 'after))  (set! ok #f))
(if (not (equal? (get_item out 3) 'thunk))  (set! ok #f))
(if (not (equal? (get_item out 4) 'before)) (set! ok #f))
(if ok
    (display "✅ PASS before call/cc\n")
    (display "❌ FAIL before call/cc\n"))
