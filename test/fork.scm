(native System)

(define pid (System.fork))
(if (== 0 pid) {
  (display "child")
} {
  (System.set_interval 1000 (lambda () (newline) (display "Heartbeat @ ") (display (System.timestamp)) (newline)))
})

