# Kestrel

> A sleek, fast, lightweight bytecode-compiled language and stack-based virtual machine written in C11.

Inspired by the architecture of Bob Nystrom's *Crafting Interpreters*, **Kestrel** is built incrementally with modular subsystems: a single-pass maximal-munch scanner, a Pratt parser compiler (Milestone 2), a compact 24-opcode stack machine VM, and a mark-and-sweep garbage collector (Milestone 4).

---

## Documentation

* **[Architecture & VM Internals](docs/architecture.md)**: System design, memory model, tagged union representations, stack mechanics, and error handling.
* **[Instruction Set Architecture (ISA) Reference](docs/isa.md)**: Complete specification for all 24 opcodes, binary encoding conventions, jump arithmetic, and disassembly formats.

---

## Quick Start

### Build & Test

```sh
# Compile the Kestrel binary
make

# Run the automated test suite (tests/run_tests.sh)
make test
```

### Run

```sh
# Start the REPL (read-eval-print loop)
./kestrel

# Compile and run a source file
./kestrel examples/hello.txt

# Run hand-assembled bytecode demo (disassembles and evaluates)
./kestrel demo

# Lex a source file and inspect its token stream
./kestrel --lex examples/hello.txt
```

---

## Project Structure

```text
.
├── .github/workflows/    # Continuous Integration (CI) configuration
│   └── ci.yml
├── docs/                 # Detailed architectural and ISA documentation
│   ├── architecture.md
│   └── isa.md
├── examples/             # Sample source programs
│   └── hello.txt
├── src/                  # C11 engine source code
│   ├── chunk.h / .c      # Bytecode chunk buffer & constant pool
│   ├── common.h          # Shared standard library headers & debug flags
│   ├── compiler.h / .c   # Pratt parser, scopes, functions, closures
│   ├── debug.h / .c      # Bytecode disassembler & operand decoder
│   ├── main.c            # CLI entry point (REPL, run, lex, demo)
│   ├── memory.h / .c     # reallocate() + mark-and-sweep GC
│   ├── object.h / .c     # ObjString/Function/Closure/Upvalue
│   ├── scanner.h / .c    # Lexical scanner (maximal-munch, zero-copy tokens)
│   ├── table.h / .c      # Hash table (globals + string interning)
│   ├── value.h / .c      # Tagged-union Value and dynamic ValueArray
│   └── vm.h / .c         # Stack VM, call frames, upvalues & runtime checks
├── tests/                # Automated regression test suite
│   ├── fixtures/
│   └── run_tests.sh
├── .gitignore            # Git ignore patterns for build & OS artifacts
├── CONTRIBUTING.md       # Contribution guidelines & coding standards
├── LICENSE               # MIT License
├── Makefile              # Modular build system & test runner
└── README.md             # Project overview & roadmap
```

---

## Key Design Decisions

1. **Tagged Union First, NaN-Boxing Later**: Values are represented as a tagged union (`ValueType` + union `as`). This allows immediate, transparent debugging. Because access is wrapped in `IS_*` and `AS_*` macros, NaN-boxing can be dropped in later without modifying bytecode or compiler logic.
2. **Strict, Clean Truthiness**: Only `false` and `nil` are falsy. Numbers (including `0.0`), empty strings, and objects are always truthy.
3. **Decoupled Scanner**: The scanner emits raw token slices pointing directly into the source buffer (`const char* start`, `length`). String unescaping and numeric parsing occur during compilation.
4. **Defensive Runtime Safety**: The VM checks stack bounds via debug assertions, validates dynamic operand types before executing arithmetic, and produces line-numbered runtime error reports with stack unwinding.
5. **Standardized Exit Codes**: Conforms to BSD `<sysexits.h>`: `64` (`EX_USAGE`), `65` (`EX_DATAERR`), `70` (`EX_SOFTWARE`), `74` (`EX_IOERR`).

---

## Debugging

To observe the VM's inner execution loop, enable instruction tracing in [`src/common.h`](file:///Users/aritra/Code/Languages/C/Project-1/src/common.h):

```c
#define DEBUG_TRACE_EXECUTION
```

When enabled, the VM prints the entire operand stack before every instruction and disassembles the active opcode:

```text
== demo chunk ==
0000  123 OP_CONSTANT         0 '1.2'
0002    | OP_CONSTANT         1 '3.4'
0004    | OP_ADD
0005    | OP_NEGATE
0006    | OP_PRINT
0007    | OP_NIL
0008    | OP_RETURN
           [ 1.2 ]
           [ 1.2 ][ 3.4 ]
           [ 4.6 ]
           [ -4.6 ]
-4.6
```

---

## Roadmap

| # | Milestone | Scope | Status | Deliverables |
| :-: | :--- | :--- | :-: | :--- |
| **1** | **Foundations** | ISA, Value/Chunk/VM structures, scanner, disassembler, minimal VM over arithmetic | **Completed** | Disassembler, arithmetic execution (`./kestrel demo`), file lexing, regression tests. |
| **2** | **Compiler** | Pratt parser (precedence climbing), expressions, statements (`var`, `if/else`, `while`, `print`), jump backpatching, strings | **Completed** | `./kestrel <file>` compiles source directly into bytecode and executes it. |
| **3** | **Functions & Closures** | `fun` declarations, `OP_CALL`, stack call frames, recursion, upvalues, `OP_CLOSURE`, stack traces | **Completed** | Recursive `fib(n)` and closure-based state encapsulation. |
| **4** | **Garbage Collector** | Hash table globals/interning, mark-and-sweep GC via `reallocate()`, stress-test mode, REPL | **Completed** | Stability under `DEBUG_STRESS_GC` without leaks; `./kestrel` REPL; `bench_fib` benchmark. |

---

## License & Acknowledgments

* Architecture inspired by Bob Nystrom's [Crafting Interpreters](https://craftinginterpreters.com/).
* Written in ISO C11.