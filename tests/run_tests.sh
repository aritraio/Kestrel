#!/usr/bin/env bash
# Lightweight test suite for the kestrel VM (Milestones 2-4: compiler, functions, GC).
# Run via: make test   (or directly: ./tests/run_tests.sh)
set -u

cd "$(dirname "$0")/.."

BIN=./kestrel
FIXTURES=tests/fixtures

pass=0
fail=0

# report <name> <status>   status 0 = pass, anything else = fail
report() {
  if [ "$2" -eq 0 ]; then
    echo "PASS: $1"
    pass=$((pass + 1))
  else
    echo "FAIL: $1"
    fail=$((fail + 1))
  fi
}

###############################################################################
# 1. demo mode: disassembles a chunk, prints -4.6, exits 0
###############################################################################
out=$("$BIN" demo 2>&1)
code=$?
status=0
[ "$code" -eq 0 ]                          || status=1
printf '%s' "$out" | grep -q -- "-4.6"     || status=1
report "demo runs, prints -4.6, exits 0" "$status"

###############################################################################
# 2. examples/hello.txt: lexes all expected tokens, no errors, exit 0
###############################################################################
out=$("$BIN" --lex examples/hello.txt 2>&1)
code=$?
status=0
[ "$code" -eq 0 ]                          || status=1
printf '%s' "$out" | grep -q "'var'"       || status=1
printf '%s' "$out" | grep -q "'fun'"       || status=1
printf '%s' "$out" | grep -q "'while'"     || status=1
printf '%s' "$out" | grep -q "'print'"     || status=1
printf '%s' "$out" | grep -q "EOF"         || status=1
printf '%s' "$out" | grep -q "ERROR"       && status=1
report "hello.txt produces expected tokens" "$status"

###############################################################################
# 3. string escapes: escaped quotes/backslashes stay inside one STRING token
###############################################################################
out=$("$BIN" --lex "$FIXTURES/strings.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ]                                    || status=1
printf '%s' "$out" | grep -q "ERROR"                 && status=1
printf '%s' "$out" | grep -qF 'hello \"world\"'      || status=1
n_strings=$(printf '%s' "$out" | grep -c "STRING")
[ "$n_strings" -eq 1 ]                               || status=1
report "escaped quotes lex as a single STRING token" "$status"

###############################################################################
# 4. lex errors: unterminated string -> ERROR + exit 65
###############################################################################
out=$("$BIN" --lex "$FIXTURES/unterminated_string.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 65 ]                                   || status=1
printf '%s' "$out" | grep -q "ERROR"                 || status=1
printf '%s' "$out" | grep -qF "Unterminated string." || status=1
report "unterminated string reports error, exits 65" "$status"

###############################################################################
# 5. lex errors: invalid number literal ("123abc") -> ERROR + exit 65
###############################################################################
out=$("$BIN" --lex "$FIXTURES/bad_number.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 65 ]                                   || status=1
printf '%s' "$out" | grep -q "ERROR"                 || status=1
printf '%s' "$out" | grep -qF "Invalid number literal." || status=1
report "123abc reports Invalid number literal, exits 65" "$status"

###############################################################################
# 6. lex errors: unexpected character -> ERROR + exit 65
###############################################################################
out=$("$BIN" --lex "$FIXTURES/bad_char.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 65 ]                                   || status=1
printf '%s' "$out" | grep -q "ERROR"                 || status=1
printf '%s' "$out" | grep -qF "Unexpected character." || status=1
report "invalid character reports error, exits 65" "$status"

###############################################################################
# 7. Pratt precedence: 1+2*3=7, (1+2)*3=9, 1+2*3-4/2=5, (1+2)*(3-1)=6, 4/2=2
###############################################################################
out=$("$BIN" "$FIXTURES/expr_arith.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="7
9
5
6
2"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "arithmetic precedence and grouping" "$status"

###############################################################################
# 8. unary / logical-not: -5, -1*-2=2, !false=true, !true=false, etc.
###############################################################################
out=$("$BIN" "$FIXTURES/expr_unary.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="-5
2
true
false
true
false
5"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "unary minus and logical not" "$status"

###############################################################################
# 9. comparisons and equality (incl. desugared <= >= !=)
###############################################################################
out=$("$BIN" "$FIXTURES/expr_comparison.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="true
true
true
true
true
false
true
true
true
true"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "equality and comparison operators" "$status"

###############################################################################
# 10. literals and expression-statement pop (bare `1+2;` prints nothing)
###############################################################################
out=$("$BIN" "$FIXTURES/expr_literals.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="true
false
nil
123
3.14
3"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "bool/nil/number literals and OP_POP" "$status"

###############################################################################
# 11. compile errors: `print 1 + ;` -> diagnostic with line number, exit 65
###############################################################################
out=$("$BIN" "$FIXTURES/expr_error.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 65 ]                                     || status=1
printf '%s' "$out" | grep -q "Error"                   || status=1
printf '%s' "$out" | grep -q "\[line 1\]"              || status=1
report "syntax error reports line number, exits 65" "$status"

###############################################################################
# 12. runtime errors: `print 1 + true;` -> type error, exit 70
###############################################################################
out=$("$BIN" "$FIXTURES/expr_runtime.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 70 ]                                     || status=1
printf '%s' "$out" | grep -q "Operands must be two numbers" || status=1
printf '%s' "$out" | grep -q "\[line 1\]"              || status=1
report "type error reports line number, exits 70" "$status"

###############################################################################
# 13. variables: globals, assignment, nil default, block shadowing
###############################################################################
out=$("$BIN" "$FIXTURES/vars.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="3
10
nil
12
99
100
10"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "var declaration, assignment and shadowing" "$status"

###############################################################################
# 14. strings: literals, concatenation, equality
###############################################################################
out=$("$BIN" "$FIXTURES/strings_exec.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="hello
hello world
foobar
foo foo

true
true
true"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "string literals, concat and equality" "$status"

###############################################################################
# 15. if/else branching
###############################################################################
out=$("$BIN" "$FIXTURES/if_else.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="yes
good
after
1
lt"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "if/else control flow" "$status"

###############################################################################
# 16. while loops (including false condition skips body)
###############################################################################
out=$("$BIN" "$FIXTURES/while_loop.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="0
1
2
done
3
2
1
end"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "while loops with backpatching" "$status"

###############################################################################
# 17. and/or with short-circuit (false and x / true or x do not error)
###############################################################################
out=$("$BIN" "$FIXTURES/logic.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="true
false
false
true
false
hi
42
2
false
true"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "and/or short-circuit logic" "$status"

###############################################################################
# 18. compile error: local read in own initializer -> 65
###############################################################################
out=$("$BIN" "$FIXTURES/scope_self_init.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 65 ]                                        || status=1
printf '%s' "$out" | grep -q "own initializer"            || status=1
printf '%s' "$out" | grep -q "\[line 2\]"                 || status=1
report "self-initializer rejected, exits 65" "$status"

###############################################################################
# 19. compile error: duplicate local in same scope -> 65
###############################################################################
out=$("$BIN" "$FIXTURES/scope_redeclare.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 65 ]                                        || status=1
printf '%s' "$out" | grep -q "Already a variable"         || status=1
report "duplicate local rejected, exits 65" "$status"

###############################################################################
# 20. runtime error: undefined variable -> 70
###############################################################################
out=$("$BIN" "$FIXTURES/undef_var.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 70 ]                                        || status=1
printf '%s' "$out" | grep -q "Undefined variable"         || status=1
printf '%s' "$out" | grep -q "\[line 1\]"                 || status=1
report "undefined variable reports error, exits 70" "$status"

###############################################################################
# 21. runtime error: number + string mix -> 70
###############################################################################
out=$("$BIN" "$FIXTURES/mixed_add.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 70 ]                                        || status=1
printf '%s' "$out" | grep -q "two numbers or two strings" || status=1
printf '%s' "$out" | grep -q "\[line 1\]"                 || status=1
report "mixed-type add reports error, exits 70" "$status"

###############################################################################
# 22. functions: call, return value, bare/nil return
###############################################################################
out=$("$BIN" "$FIXTURES/fun_simple.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="3
nil
1
99"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "fun declaration, call and return" "$status"

###############################################################################
# 23. recursion: fib(0)=0, fib(1)=1, fib(10)=55
###############################################################################
out=$("$BIN" "$FIXTURES/fun_fib.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="0
1
55"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "recursive fib works" "$status"

###############################################################################
# 24. closures: capture + independent counters (open/closed upvalues)
###############################################################################
out=$("$BIN" "$FIXTURES/closure_counter.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="10
1
2
1"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "closures capture locals, counters stay independent" "$status"

###############################################################################
# 25. closed upvalues: block local hoisted, set/get through upvalue
###############################################################################
out=$("$BIN" "$FIXTURES/closure_closed.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="1
2"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "closed upvalues survive scope exit" "$status"

###############################################################################
# 26. runtime error: arity mismatch -> 70 with line
###############################################################################
out=$("$BIN" "$FIXTURES/fun_arity.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 70 ]                                   || status=1
printf '%s' "$out" | grep -q "Expected 1 arguments"   || status=1
printf '%s' "$out" | grep -q "\[line 4\]"             || status=1
report "arity mismatch reports error, exits 70" "$status"

###############################################################################
# 27. compile error: return at top level -> 65
###############################################################################
out=$("$BIN" "$FIXTURES/fun_topreturn.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 65 ]                                   || status=1
printf '%s' "$out" | grep -q "top-level"              || status=1
report "top-level return rejected, exits 65" "$status"

###############################################################################
# 28. runtime error: call non-function -> 70
###############################################################################
out=$("$BIN" "$FIXTURES/call_nonfn.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 70 ]                                   || status=1
printf '%s' "$out" | grep -q "Can only call"         || status=1
report "calling non-function reports error, exits 70" "$status"

###############################################################################
# 29. stack trace: a()->b()->c() error shows all frames
###############################################################################
out=$("$BIN" "$FIXTURES/stack_trace.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 70 ]                                   || status=1
printf '%s' "$out" | grep -q "in c()"                || status=1
printf '%s' "$out" | grep -q "in b()"                || status=1
printf '%s' "$out" | grep -q "in a()"                || status=1
printf '%s' "$out" | grep -q "in script"             || status=1
report "runtime stack trace lists call frames" "$status"

###############################################################################
# 30. interning: identical literals share one object (== via identity)
###############################################################################
out=$("$BIN" "$FIXTURES/strings_exec.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
printf '%s' "$out" | grep -q "^true$"                || status=1
report "string interning keeps equality true" "$status"

###############################################################################
# 31. benchmark: fib(20) = 6765 completes
###############################################################################
out=$("$BIN" "$FIXTURES/bench_fib.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
[ "$(printf '%s' "$out")" = "6765" ]                 || status=1
report "bench fib(20) prints 6765" "$status"

###############################################################################
# 32. GC churn: 200 concatenations survive collection
###############################################################################
out=$("$BIN" "$FIXTURES/bench_gc.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
expected="true
200"
[ "$(printf '%s' "$out")" = "$expected" ] || status=1
report "string churn survives mark-and-sweep" "$status"

###############################################################################
# 33. REPL: piped input evaluates and exits 0
###############################################################################
out=$(printf 'print 1 + 2;\nprint "hi";\n' | "$BIN" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
printf '%s' "$out" | grep -q "3"                     || status=1
printf '%s' "$out" | grep -q "hi"                    || status=1
printf '%s' "$out" | grep -q ">"                     || status=1
report "REPL evaluates piped lines" "$status"

###############################################################################
# 34. REPL: survives a syntax error and keeps going
###############################################################################
out=$(printf 'print ;\nprint 3;\n' | "$BIN" 2>&1)
code=$?
status=0
[ "$code" -eq 0 ] || status=1
printf '%s' "$out" | grep -q "Error"                 || status=1
printf '%s' "$out" | grep -q " 3"                    || status=1
report "REPL continues past compile errors" "$status"

###############################################################################

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
