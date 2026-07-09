native(Math);
native(System);
native(String);
System.eval("(display \"灵机解释器 JavaScript解释执行演示\")");
newline();
var count = 0;
var prompt = ["复读机！", "路由器！"];
System.set_interval(1000, ()=> {
    System.eval("(display \"[\")");
    System.eval("(display count)");
    System.eval("(display \"] \")");
    var rnd = Math.random();
    if (rnd > 0.5) {
        rnd = 0;
    }
    else {
        rnd = 1;
    }
    var str = String.concat("人类的本质是", prompt[rnd]);
    display(str);
    newline();
    count++;
});





function fac(n) {
    if (n <= 1) {
        return 1;
    } else {
        return n * fac(n - 1);
    }
}
var result = fac(10);

display("fac(10) = ");
display(result);
newline();





function sqrsum(a, b) {
    return Math.pow(a, 2) + Math.pow(b, 2);
}

var x = Math.sqrt(sqrsum(3, 4));

display("sqrt(3^2 + 4^2) = ");
display(x);
newline();




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
display("2^10 = ");
display(p);
newline();


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

var lst = [1, 1, 4, 5, 1, 4];
bubble_sort(lst, (x, y)=>{ return x > y; })
display(lst);


