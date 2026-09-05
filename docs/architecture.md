# Kestrel Architecture & VM Internals

This document details the architectural design, internal memory models, and execution pipeline of the **Kestrel** bytecode programming language.

---

## 1. High-Level System Architecture

Kestrel is organized into distinct, modular compiler and runtime phases. The architecture decouples lexical analysis from bytecode compilation, and bytecode compilation from runtime execution.

```mermaid
flowchart TD
    subgraph Frontend [Compiler Pipeline]
        Source["Source Text (.txt / .ks)"] --> Scanner["Scanner (scanner.c)"]
        Scanner -->|"Token Stream (zero-copy)"| Compiler["Pratt Compiler (compiler.c)"]
        Compiler -->|"ObjFunction + Bytecode & Constants"| Chunk["Chunk (chunk.c)"]
    end

    subgraph Backend [Runtime Engine]
        Chunk -->|"Disassembles"| Disassembler["Disassembler (debug.c)"]
        Chunk -->|"Executes"| VM["Virtual Machine (vm.c)"]
        VM <-->|"Frames & Locals"| Stack["Value Stack + CallFrames (64 frames)"]
        VM <-->|"Globals / Interning"| Table["Hash Table (table.c)"]
        VM <-->|"Allocations"| Memory["Memory Manager + GC (memory.c)"]
        Memory <-->|"Heap Objects"| Objects["ObjString/Function/Closure/Upvalue (object.c)"]
    end
```

---

## 2. Compilation Subsystem

### 2.1 The Scanner (`scanner.h`, `scanner.c`)
The scanner is a single-pass, maximal-munch lexer that streams tokens on-demand via `scanToken()`.

#### Key Design Decisions:
* **Zero-Copy Tokens**: The `Token` struct stores a pointer directly into the original source buffer (`const char* start`) and an integer `length`. It does not allocate heap memory for lexemes.
* **Separation of Concerns**: The scanner does not convert literals into runtime values (e.g. converting `"3.14"` to `double` or `\"hello\"` to `ObjString`). Literal conversion is deferred entirely to the compiler.
* **Unicode / Non-ASCII Safety**: Standard `<ctype.h>` functions (`isalpha`, `isdigit`) exhibit undefined behavior when passed negative signed `char` values. All calls in `scanner.c` explicitly cast to `(unsigned char)`.
* **String Escape Sequences**: Strings allow backslash escapes (`\"`, `\\`, `\n`, `\t`, `\r`). The scanner consumes the escaped character without prematurely closing the string, preserving source integrity for the compiler unescape phase.
* **Syntax Guarding**: Malformed number literals like `123abc` are captured at the lexical boundary as `TOKEN_ERROR` ("Invalid number literal."), preventing ambiguous identifier splits.

---

## 3. Bytecode Representation (`chunk.h`, `chunk.c`)

A `Chunk` represents a compiled bytecode sequence (such as a top-level script or a function body).

```c
typedef struct {
  int count;              // Number of bytecode bytes currently written
  int capacity;           // Allocated bytecode buffer capacity
  uint8_t* code;          // Dynamic array of instruction opcodes & operands
  int* lines;             // Parallel array of source line numbers for debug info
  ValueArray constants;   // Constant pool storing literals referenced by bytecode
} Chunk;
```

### Constant Pool Constraints
* Bytecode instructions (such as `OP_CONSTANT`) address the constant pool using a single-byte `uint8_t` operand.
* `addConstant()` checks `constants.count >= UINT8_COUNT` (256) and exits with `EX_SOFTWARE` (70) rather than silently truncating higher indices to 8 bits.

### Dynamic Memory Growth
All dynamic buffers (`code`, `lines`, `constants.values`) grow exponentially via `GROW_CAPACITY`:
$$\text{capacity} = \begin{cases} 8 & \text{if capacity} < 8 \\ \text{capacity} \times 2 & \text{otherwise} \end{cases}$$

---

## 4. Value Representation & Object Model (`value.h`, `object.h`)

### 4.1 Tagged Union Value
Kestrel uses an explicit tagged-union representation for runtime values.

```c
typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ
} ValueType;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj* obj;
  } as;
} Value;
```

#### Rationale: Tagged Union vs. NaN-Boxing
* **Clarity First**: The tagged union provides straightforward debugging and simple C memory inspection.
* **Drop-in Upgrade**: Because all code interacts with values via `IS_*` and `AS_*` accessor macros, the project can transition to NaN-boxing in the future without altering opcode definitions or the compiler.

### 4.2 Truthiness Semantics
Kestrel adheres to a clean, unambiguous truthiness rule:
* **Falsy**: Only `nil` and `false`.
* **Truthy**: Everything else, including `0.0`, empty strings, and functions.

```c
#define IS_FALSY(value) \
  (IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value)))
```

### 4.3 Heap Objects & Memory Headers
All heap-allocated entities share an `Obj` header:

```c
struct sObj {
  ObjType type;          // OBJ_STRING / FUNCTION / CLOSURE / UPVALUE
  bool isMarked;         // Mark bit for the mark-and-sweep collector
  struct sObj* next;     // Intrusive singly-linked list of all allocated objects
};
```

Strings are allocated inline using C99 flexible array members, functions own
their chunk, closures point at a function plus captured upvalues, and open
upvalues point into the VM stack until closed:

```c
struct sObjString {
  Obj obj;
  int length;
  uint32_t hash;         // FNV-1a, used for interning
  char chars[];          // Flexible array member: character data stored inline
};
typedef struct {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString* name;       // NULL for top-level script
} ObjFunction;
typedef struct sObjUpvalue {
  Obj obj;
  Value* location;       // stack slot (open) or &closed (closed)
  Value closed;
  struct sObjUpvalue* next;
} ObjUpvalue;
typedef struct {
  Obj obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int upvalueCount;
} ObjClosure;
```

String interning guarantees identical literals share one `ObjString`, so
`valuesEqual` for objects is identity. During compilation fresh strings and
functions are rooted on the VM stack across `makeConstant` (see `compiler.c`)
so a collection triggered by constant/chunk growth cannot free them before
they land in the chunk.

---

## 5. Virtual Machine (`vm.h`, `vm.c`)

The Kestrel VM is a stack-based abstract machine with call frames and a
single global state instance (`VM vm`).

```c
typedef struct {
  ObjClosure* closure;
  uint8_t* ip;                       // Instruction pointer into function chunk
  Value* slots;                      // Window into vm.stack for locals/temps
} CallFrame;
typedef struct {
  CallFrame frames[FRAMES_MAX];      // 64 frames
  int frameCount;
  Value stack[STACK_MAX];            // 64*256 slots: operands + locals
  Value* stackTop;
  Obj* objects;                      // All heap objects (GC list)
  Table globals;                     // Globals by interned name
  Table strings;                     // Intern pool (weak)
  ObjUpvalue* openUpvalues;          // Sorted by stack address
  size_t bytesAllocated, nextGC;     // GC threshold (starts 1 MiB, doubles)
  Obj** grayStack; int grayCount, grayCapacity;
} VM;
```

### 5.1 Stack Discipline & Safety
1. **Assertions**: `push()` and `pop()` assert stack bounds in debug builds (`vm.stackTop < vm.stack + STACK_MAX` and `vm.stackTop > vm.stack`).
2. **Peeking**: `peek(int distance)` accesses values relative to the stack top without popping them (`vm.stackTop[-1 - distance]`).
3. **Frames**: `call()` checks arity (`Expected %d arguments but got %d.`) and `FRAMES_MAX` (`Stack overflow.`); `callValue()` rejects non-closures (`Can only call functions and classes.`). Locals are `frame->slots[slot]`; slot 0 is reserved for the closure itself.
4. **Upvalues**: `captureUpvalue()` dedups sorted open upvalues; `OP_CLOSE_UPVALUE` hoists `*location` to `closed` when the local leaves scope, so returned closures keep working.
5. **Dynamic Type Validation**: Arithmetic checks `IS_NUMBER()` / string concat checks `IS_STRING()` before operating, else `Operands must be two numbers or two strings.`

### 5.2 Error Reporting & Stack Unwinding
When a runtime error occurs:
```c
static void runtimeError(const char* format, ...) {
  // 1. Prints formatted diagnostic message to stderr
  // 2. Walks frames: "[line N] in fn()" per frame ("script" for top level)
  // 3. Resets stackTop/frameCount/openUpvalues to prevent corruption
}
```
`OP_JUMP_IF_FALSE` peeks (does not pop); the compiler emits an explicit
`OP_POP` on each branch so `and`/`or` can leave short-circuited values.

---

## 6. Memory Management & Garbage Collector (`memory.h`, `memory.c`, `table.h`)

All dynamic allocations, reallocations, and deallocations pass through a single entry point:

```c
void* reallocate(void* pointer, size_t oldSize, size_t newSize);
```

| Operation | `pointer` | `oldSize` | `newSize` | Behavior |
| :--- | :--- | :--- | :--- | :--- |
| **Allocate** | `NULL` | `0` | $> 0$ | Runs GC if over threshold, then `realloc(NULL, newSize)`. |
| **Grow** | Existing buffer | Current bytes | New bytes | Runs GC if growing over threshold, then `realloc`. |
| **Free** | Existing buffer | Current bytes | `0` | Calls `free(pointer)`, returns `NULL` (never triggers GC). |

The collector is mark-and-sweep with a tri-color worklist (`grayStack`, raw
`realloc` to avoid re-entrancy):
1. **Mark roots**: VM stack, frames (closures), `openUpvalues`, `globals`
   (via `markTable`), compiler chain (`markCompilerRoots` for in-progress
   functions).
2. **Trace**: pop gray objects, `blackenObject` (closure→function+upvalues,
   function→name+constants, upvalue→closed).
3. **Sweep**: free unmarked objects via `freeObject`, clear marks; then
   `tableRemoveWhite(&vm.strings)` drops unreachable interned strings.
4. **Threshold**: `nextGC = bytesAllocated * 2` (starts 1 MiB).
`DEBUG_STRESS_GC` collects on every growing allocation;
`DEBUG_LOG_GC` logs begin/end bytes. The suite passes under both normal and
stress modes plus ASan/UBSan.

Hash tables (`table.c`) are open-addressing with linear probing, tombstones
(`key NULL` + `true`), `TABLE_MAX_LOAD 0.75`, FNV-1a hashing, big-endian-safe
`memcmp` lookups for interning (`tableFindString`) and globals.

---

## 7. Exit Codes

Kestrel follows the standard BSD `<sysexits.h>` exit conventions:

| Exit Code | Constant | Meaning |
| :---: | :--- | :--- |
| `0` | `EXIT_SUCCESS` | Execution completed without error. |
| `64` | `EX_USAGE` | Invalid command-line usage or incorrect argument count. |
| `65` | `EX_DATAERR` | Lexical error, syntax error, or compile failure in source file. |
| `70` | `EX_SOFTWARE` | Internal engine error (e.g. constant pool overflow). |
| `74` | `EX_IOERR` | File could not be opened, read, or accessed. |

---

## 8. CLI Modes (`main.c`)

* `./kestrel` — REPL: prints `> `, `fgets` each line (1024 B), `interpret()`s it, stays alive past compile/runtime errors.
* `./kestrel <file>` — compiles and runs the file (`65` on compile error, `70` on runtime error).
* `./kestrel demo` — hand-assembles `-(1.2+3.4)`, disassembles, runs (ends with `OP_NIL` for frame `OP_RETURN`).
* `./kestrel --lex <file>` — token dump (`65` on lex error); preserves Milestone 1 behavior behind a flag.
