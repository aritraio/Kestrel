#!/usr/bin/env bash
# Lightweight test suite for the kestrel VM (Milestone 2: compiler).
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

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
