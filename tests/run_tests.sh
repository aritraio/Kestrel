#!/usr/bin/env bash
# Lightweight test suite for the kestrel VM (Milestone 1).
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
out=$("$BIN" examples/hello.txt 2>&1)
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
out=$("$BIN" "$FIXTURES/strings.txt" 2>&1)
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
out=$("$BIN" "$FIXTURES/unterminated_string.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 65 ]                                   || status=1
printf '%s' "$out" | grep -q "ERROR"                 || status=1
printf '%s' "$out" | grep -qF "Unterminated string." || status=1
report "unterminated string reports error, exits 65" "$status"

###############################################################################
# 5. lex errors: invalid number literal ("123abc") -> ERROR + exit 65
###############################################################################
out=$("$BIN" "$FIXTURES/bad_number.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 65 ]                                   || status=1
printf '%s' "$out" | grep -q "ERROR"                 || status=1
printf '%s' "$out" | grep -qF "Invalid number literal." || status=1
report "123abc reports Invalid number literal, exits 65" "$status"

###############################################################################
# 6. lex errors: unexpected character -> ERROR + exit 65
###############################################################################
out=$("$BIN" "$FIXTURES/bad_char.txt" 2>&1)
code=$?
status=0
[ "$code" -eq 65 ]                                   || status=1
printf '%s' "$out" | grep -q "ERROR"                 || status=1
printf '%s' "$out" | grep -qF "Unexpected character." || status=1
report "invalid character reports error, exits 65" "$status"

###############################################################################

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]