// jstest.js - JavaScript test cases for jstoscm.c
// Each test is a standalone snippet that can be fed to the translator.
// Markers begin with "// --- TEST:".
// An EXPECT block follows each snippet, showing the exact Scheme output.

// --- TEST: empty ---
// (this test contains no statements)
/* EXPECT:
((lambda ()))
*/

// --- TEST: arithmetic ---
var result = 1 + 2 * 3 - 4 / 2 % 3;
/* EXPECT:
((lambda ()
  (define result (- (+ 1 (* 2 3)) (mod (/ 4 2) 3)))))
*/

// --- TEST: precedence_and_parens ---
var result = (1 + 2) * (3 - 4) / 5;
/* EXPECT:
((lambda ()
  (define result (/ (* (+ 1 2) (- 3 4)) 5))))
*/

// --- TEST: unary_and_logic ---
var a = !true;
var b = -3.14;
var c = -1e23;
var d = a && b || c;
var e = ++x;
var f = y--;
/* EXPECT:
((lambda ()
  (define a (not true))
  (define b -3.14)
  (define c -1e23)
  (define d (or (and a b) c))
  (define e (set! x (+ x 1)))
  (define f (set! y (- y 1)))))
*/

// --- TEST: literals ---
var a;
var b = 1;
var c = undefined;
var d = null;
var e = true;
var f = false;
/* EXPECT:
((lambda ()
  (define a undefined)
  (define b 1)
  (define c undefined)
  (define d null)
  (define e true)
  (define f false)))
*/

// --- TEST: assignment_chain ---
var a = 1;
var b = a = 2;
/* EXPECT:
((lambda ()
  (define a 1)
  (define b (set! a 2))))
*/

// --- TEST: list_literal_and_index ---
var lst = [1, 2, 3];
var empty = [];
var nested = [1, [2, 3]];
lst[0] = 42;
var first = lst[0];
/* EXPECT:
((lambda ()
  (define lst '(1 2 3))
  (define empty '())
  (define nested '(1 '(2 3)))
  (set_item! lst 0 42)
  (define first (get_item lst 0))))
*/

// --- TEST: string_and_escapes ---
var s = "hello";
var t = "line1\nline2";
var u = "quote \" inside";
/* EXPECT:
((lambda ()
  (define s "hello")
  (define t "line1\nline2")
  (define u "quote \" inside")))
*/

// --- TEST: if_else ---
var x = 1;
if (x > 0) {
    x = x + 1;
} else {
    x = x - 1;
}
/* EXPECT:
((lambda ()
  (define x 1)
  (if (> x 0) {(set! x (+ x 1))} {(set! x (- x 1))})))
*/

// --- TEST: if_elseif_else ---
var score = 85;
if (score >= 90) {
    grade = "A";
} else if (score >= 60) {
    grade = "B";
} else {
    grade = "C";
}
/* EXPECT:
((lambda ()
  (define score 85)
  (if (>= score 90) {(set! grade "A")} (if (>= score 60) {(set! grade "B")} {(set! grade "C")}))))
*/

// --- TEST: while_break_continue ---
var i = 0;
while (i < 10) {
    i = i + 1;
    if (i == 3) { continue; }
    if (i == 7) { break; }
}
/* EXPECT:
((lambda ()
  (define i 0)
  (while (< i 10) {(set! i (+ i 1)) (if (== i 3) {continue}) (if (== i 7) {break})})))
*/

// --- TEST: factorial (based on test/factorial.scm) ---
function fac(n) {
    if (n <= 1) {
        return 1;
    } else {
        return n * fac(n - 1);
    }
}
var result = fac(5);
/* EXPECT:
((lambda ()
  (define fac (lambda (n) (if (<= n 1) {1} {(* n (fac (- n 1)))})))
  (define result (fac 5))))
*/

// --- TEST: sum_iter (based on test/factorial.scm) ---
function sum_iter(n, init) {
    if (n == 0) {
        return init;
    } else {
        return sum_iter(n - 1, n + init);
    }
}
var total = sum_iter(100, 0);
/* EXPECT:
((lambda ()
  (define sum_iter (lambda (n init) (if (== n 0) {init} {(sum_iter (- n 1) (+ n init))})))
  (define total (sum_iter 100 0))))
*/

// --- TEST: power (based on test/factorial.scm, cond -> if-else) ---
function power(base, exp, init) {
    if (exp == 0) {
        return init;
    } else {
        if (exp % 2 == 0) {
            return power(base * base, exp / 2, init);
        } else {
            return power(base, exp - 1, base * init);
        }
    }
}
var p = power(2, 10, 1);
/* EXPECT:
((lambda ()
  (define power (lambda (base exp init) (if (== exp 0) {init} {(if (== (mod exp 2) 0) {(power (* base base) (/ exp 2) init)} {(power base (- exp 1) (* base init))})})))
  (define p (power 2 10 1))))
*/

// --- TEST: lambda_higher_order ---
function make_adder(x) {
    return (y) => { return x + y; };
}
var add5 = make_adder(5);
var result = add5(3);
/* EXPECT:
((lambda ()
  (define make_adder (lambda (x) (lambda (y) (+ x y))))
  (define add5 (make_adder 5))
  (define result (add5 3))))
*/

// --- TEST: lambda_blocks ---
var f = () => {
    var a = 1;
    var b = 2;
    return a + b;
};
var g = (x) => { x = x + 1; return x; };
/* EXPECT:
((lambda ()
  (define f (lambda () (define a 1) (define b 2) (+ a b)))
  (define g (lambda (x) (set! x (+ x 1)) x))))
*/

// --- TEST: empty_and_nested_blocks ---
var a = 1;
{
}
{
    {
        a = a + 1;
    }
}
/* EXPECT:
((lambda ()
  (define a 1)
  {}
  {{(set! a (+ a 1))}}))
*/

// --- TEST: comments ---
// line comment
var a = 1;
/* block
   comment */
var b = 2; // trailing comment
/* EXPECT:
((lambda ()
  (define a 1)
  (define b 2)))
*/

// --- TEST: bubble_sort (based on test/list.scm) ---
function bubble_sort(lst, compare) {
    var n = length(lst);
    var i = 0;
    var j = 0;
    var temp = false;
    var swapped = false;
    while (i < n - 1) {
        swapped = false;
        j = 0;
        while (j < n - i - 1) {
            if (compare(get_item(lst, j), get_item(lst, j + 1))) {
                temp = lst[j];
                lst[j] = lst[j + 1];
                lst[j + 1] = temp;
                swapped = true;
            }
            j = j + 1;
        }
        if (!swapped) { break; }
        i = i + 1;
    }
}
/* EXPECT:
((lambda ()
  (define bubble_sort (lambda (lst compare) (define n (length lst)) (define i 0) (define j 0) (define temp false) (define swapped false) (while (< i (- n 1)) {(set! swapped false) (set! j 0) (while (< j (- (- n i) 1)) {(if (compare (get_item lst j) (get_item lst (+ j 1))) {(set! temp (get_item lst j)) (set_item! lst j (get_item lst (+ j 1))) (set_item! lst (+ j 1) temp) (set! swapped true)}) (set! j (+ j 1))}) (if (not swapped) {break}) (set! i (+ i 1))})))))
*/

// --- TEST: list_set (based on test/list.scm) ---
function list_set_iter(lst, pos, new_value, iter) {
    if (iter == pos) {
        return cons(new_value, cdr(lst));
    } else {
        return cons(car(lst), list_set_iter(cdr(lst), pos, new_value, iter + 1));
    }
}
function list_set(lst, pos, new_value) {
    return list_set_iter(lst, pos, new_value, 0);
}
/* EXPECT:
((lambda ()
  (define list_set_iter (lambda (lst pos new_value iter) (if (== iter pos) {(cons new_value (cdr lst))} {(cons (car lst) (list_set_iter (cdr lst) pos new_value (+ iter 1)))})))
  (define list_set (lambda (lst pos new_value) (list_set_iter lst pos new_value 0)))))
*/

// --- TEST: blink_counter (based on test/blink.scm) ---
var counter = 0;
function delay(countdown) {
    if (countdown == 0) {
        return false;
    } else {
        return delay(countdown - 1);
    }
}
function blink() {
    console.log(counter % 2);
    counter = counter + 1;
    if (counter % 2 == 0) {
        delay(1000);
    } else {
        delay(2000);
    }
    blink();
}
/* EXPECT:
((lambda ()
  (define counter 0)
  (define delay (lambda (countdown) (if (== countdown 0) {false} {(delay (- countdown 1))})))
  (define blink (lambda () (console.log (mod counter 2)) (set! counter (+ counter 1)) (if (== (mod counter 2) 0) {(delay 1000)} {(delay 2000)}) (blink)))))
*/

// --- TEST: ternary ---
var sign = x > 0 ? 1 : (x < 0 ? -1 : 0);
/* EXPECT:
((lambda ()
  (define sign (if (> x 0) 1 (if (< x 0) -1 0)))))
*/

// --- TEST: function_body_without_return ---
function greet(name) {
    console.log("hello");
    console.log(name);
}
/* EXPECT:
((lambda ()
  (define greet (lambda (name) (console.log "hello") (console.log name)))))
*/

// --- TEST: math_identifiers (based on test/list.scm) ---
var r = Math.random();
var f = Math.floor(r * 10);
/* EXPECT:
((lambda ()
  (define r (Math.random))
  (define f (Math.floor (* r 10)))))
*/

// --- TEST: display_and_newline (based on test files) ---
display("result");
newline();
/* EXPECT:
((lambda ()
  (display "result")
  (newline)))
*/

