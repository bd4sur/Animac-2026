(define-syntax my-if
  (syntax-rules ()
    ((_ c t f) (if c t f))))

(display (my-if #t 1 2))
(newline)
(display (my-if #f 1 2))
(newline)




(define-syntax my-begin
  (syntax-rules ()
    ((_ e ...) {e ...})))

(my-begin
  (display 1)
  (display 2)
  (display 3)
  (newline))




(define tmp 100)

(define-syntax my-or
  (syntax-rules ()
    ((_ a b)
     ((lambda (tmp) (if tmp tmp b)) a))))

(display (my-or #f 42)) (newline)
(display tmp) (newline)






(define global 100)

(let-syntax ((local (syntax-rules () ((_) 42))))
  (display (local)) (newline)
  (display global) (newline))

(let-syntax ((twice (syntax-rules () ((_ body) (begin body body)))))
  (twice (display 1)))
(newline)

(display global) (newline)







(define-syntax my-when
  (syntax-rules (else)
    ((_ test e1 e2 ...) (if test (begin e1 e2 ...) #f))
    ((_ else e1 e2 ...) (begin e1 e2 ...))))

(my-when #t (display 1) (display 2))
(newline)
(my-when else (display 3))
(newline)








(define-syntax inc!
  (syntax-rules ()
    ((_ x) (set! x (+ x 1)))))

(define-syntax double-inc!
  (syntax-rules ()
    ((_ x) (begin (inc! x) (inc! x)))))

(define n 0)
(double-inc! n)
(display n) (newline)






(define-syntax unused
  (syntax-rules ()
    ((_ x) x)))
(display 42)
(newline)




