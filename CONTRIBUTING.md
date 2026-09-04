# Contributing to Kestrel

Thank you for your interest in contributing to **Kestrel**! This guide outlines how to set up the development environment, our coding standards, and how to submit contributions.

---

## 1. Development Workflow & Prerequisites

### Requirements
* A C11-compliant compiler (`clang` or `gcc`)
* GNU `make`
* `bash` (for the test runner)

### Common Make Commands
```sh
make           # Builds the kestrel binary into project root
make test      # Runs automated regression test suite
make sanitize  # Compiles and tests with AddressSanitizer and UBSan
make clean     # Removes build artifacts and compiled binaries
```

---

## 2. Code Style & Architectural Conventions

1. **Standard C11**:
   * Code must compile cleanly with `-std=c11 -Wall -Wextra -Wpedantic` without any warnings.
   * Do not use compiler-specific extensions (e.g. GCC/Clang built-ins) unless guarded by appropriate `#ifdef` checks with standard C fallbacks.

2. **Memory Management**:
   * All dynamic memory allocations, reallocations, and deallocations **must** go through `reallocate()` in [`src/memory.h`](file:///Users/aritra/Code/Languages/C/Project-1/src/memory.h).
   * Direct calls to `malloc()`, `calloc()`, `realloc()`, or `free()` are strictly prohibited outside `memory.c` and CLI file reading in `main.c`.
   * Test your changes against `make sanitize` to ensure zero memory leaks and no invalid memory accesses.

3. **Defensive Programming & Bounds Checking**:
   * Always validate dynamic array capacities before writing.
   * Enforce stack discipline via `assert()` in internal helpers, but use `runtimeError()` for user-facing syntax/type violations.
   * Cast all characters passed to `<ctype.h>` functions (`isalpha`, `isdigit`, etc.) to `(unsigned char)` to prevent undefined behavior on non-ASCII/UTF-8 bytes.

---

## 3. Adding Tests

When adding new language features or fixing bugs:
1. If the change involves syntax or lexer behavior, add or update a fixture in `tests/fixtures/`.
2. Add a corresponding test assertion in `tests/run_tests.sh`.
3. Verify that all tests pass:
   ```sh
   make test
   make sanitize
   ```

---

## 4. Submitting Pull Requests

1. Create a descriptive feature branch (`git checkout -b feature/pratt-parser`).
2. Keep commits atomic and clearly documented.
3. Verify that `make sanitize` passes with zero errors.
4. Push your branch and open a Pull Request against `main`.
