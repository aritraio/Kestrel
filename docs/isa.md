# Kestrel Instruction Set Architecture (ISA) Reference

This document provides the complete, authoritative specification for the **Kestrel** bytecode instruction set architecture.

---

## 1. Bytecode Encoding Conventions

Kestrel executes a stack-based instruction set. Bytecode is packaged in units called **Chunks** ([`src/chunk.h`](file:///Users/aritra/Code/Languages/C/Project-1/src/chunk.h)), which store bytecode streams, constant pools, and parallel source-line debugging arrays.

### Fundamental Encoding Rules
1. **Single-Byte Opcode**: Every instruction begins with a 1-byte opcode (`uint8_t`), defining 1 of the 24 ISA operations.
2. **Fixed Operands**: Each opcode is immediately followed by zero or more fixed-width operands. Instructions have predictable, fixed byte lengths (with the exception of `OP_CLOSURE` which carries a variable number of upvalue descriptors determined by the function constant).
3. **Endianness**: Multi-byte numeric operands (such as 16-bit jump offsets) are encoded in **big-endian** (most significant byte first).
4. **Constant Pool Addressing**: Constant pool indices are stored in a single byte (`uint8_t`), limiting each chunk to a maximum of **256 constants**.
5. **Local Slot & Upvalue Addressing**: Local variable slots, upvalue indices, and function argument counts are encoded as single bytes (`uint8_t`), supporting up to 256 active stack slots per frame.
6. **Jump Distances**: Jump operands are 16-bit integers (`int16_t` / `uint16_t`), supporting jump distances up to $\pm 32\text{ KB}$ within a single chunk.

---

## 2. Stack Notation

Stack effects are documented using standard Forth/JVM stack effect notation:

$$\text{before} \rightarrow \text{after}$$

* `a b -> c`: The instruction pops `b` (top of stack), pops `a` (next below top), performs an operation, and pushes result `c`.
* `-> v`: The instruction pushes `v` onto the stack without popping any values.
* `v ->`: The instruction pops and consumes `v` from the top of the stack.
* `--`: The instruction does not alter the operand stack.

---

## 3. Opcode Specification

Kestrel defines **24 opcodes** categorized into 8 functional groups.

### 3.1 Constants & Literals

| Opcode | Hex / Dec | Operands | Stack Effect | Description |
| :--- | :---: | :--- | :---: | :--- |
| `OP_CONSTANT` | `0x00` (0) | `u8 index` | `-> v` | Reads constant at `constants.values[index]` and pushes it onto the stack. Checked at runtime against pool bounds. |
| `OP_NIL` | `0x01` (1) | *none* | `-> nil` | Pushes the literal `nil` value (`VAL_NIL`). |
| `OP_TRUE` | `0x02` (2) | *none* | `-> true` | Pushes the literal boolean `true` (`VAL_BOOL`). |
| `OP_FALSE` | `0x03` (3) | *none* | `-> false` | Pushes the literal boolean `false` (`VAL_BOOL`). |

---

### 3.2 Stack Manipulation

| Opcode | Hex / Dec | Operands | Stack Effect | Description |
| :--- | :---: | :--- | :---: | :--- |
| `OP_POP` | `0x04` (4) | *none* | `v ->` | Discards the topmost value from the operand stack. Used after evaluating expression statements. |

---

### 3.3 Variable Access & Scoping

Locals reside directly in VM stack slots allocated below the function's call frame. Globals are identified by string constant names stored in the constant pool.

| Opcode | Hex / Dec | Operands | Stack Effect | Description |
| :--- | :---: | :--- | :---: | :--- |
| `OP_GET_LOCAL` | `0x05` (5) | `u8 slot` | `-> v` | Reads the value from stack slot `slot` (relative to current frame base) and pushes it onto the top of the stack. |
| `OP_SET_LOCAL` | `0x06` (6) | `u8 slot` | `v -> v` | Writes the top value to stack slot `slot` without popping it (assignment is an expression). |
| `OP_GET_GLOBAL` | `0x07` (7) | `u8 name_idx` | `-> v` | Looks up the global variable named by string constant `name_idx`. Pushes its value or triggers a runtime error if undefined. |
| `OP_DEFINE_GLOBAL` | `0x08` (8) | `u8 name_idx` | `v ->` | Defines a global variable named by string constant `name_idx` with value `v`, consuming `v` from the stack. |
| `OP_SET_GLOBAL` | `0x09` (9) | `u8 name_idx` | `v -> v` | Assigns top value `v` to the global named by string constant `name_idx` without popping `v`. Errors if undefined. |
| `OP_GET_UPVALUE` | `0x0A` (10) | `u8 upvalue_idx` | `-> v` | Reads captured closure variable from upvalue slot `upvalue_idx` and pushes it. |
| `OP_SET_UPVALUE` | `0x0B` (11) | `u8 upvalue_idx` | `v -> v` | Writes top value `v` into captured upvalue slot `upvalue_idx`. |

---

### 3.4 Logic & Comparisons

Comparisons follow strict dynamic typing. `OP_EQUAL` supports structural identity for values and interned string references.

| Opcode | Hex / Dec | Operands | Stack Effect | Description |
| :--- | :---: | :--- | :---: | :--- |
| `OP_EQUAL` | `0x0C` (12) | *none* | `a b -> bool` | Pops `b` and `a`. Pushes `true` if `valuesEqual(a, b)` is true; otherwise pushes `false`. |
| `OP_GREATER` | `0x0D` (13) | *none* | `a b -> bool` | Pops numbers `b` and `a`. Pushes `true` if $a > b$; otherwise `false`. Errors if either operand is non-numeric. |
| `OP_LESS` | `0x0E` (14) | *none* | `a b -> bool` | Pops numbers `b` and `a`. Pushes `true` if $a < b$; otherwise `false`. Errors if either operand is non-numeric. |
| `OP_NOT` | `0x13` (19) | *none* | `v -> bool` | Pops `v`. Pushes `true` if `IS_FALSY(v)` (`nil` or `false`); otherwise `false`. Numbers and strings are always truthy. |

---

### 3.5 Arithmetic & Math

All arithmetic operates on IEEE-754 64-bit floating-point numbers (`double`). Operands are type-checked dynamically; non-numeric operands emit a runtime error.

| Opcode | Hex / Dec | Operands | Stack Effect | Description |
| :--- | :---: | :--- | :---: | :--- |
| `OP_ADD` | `0x0F` (15) | *none* | `a b -> number` | Pops numbers `b` and `a`. Pushes $a + b$. *(String concatenation added in Milestone 2).* |
| `OP_SUBTRACT` | `0x10` (16) | *none* | `a b -> number` | Pops numbers `b` and `a`. Pushes $a - b$. |
| `OP_MULTIPLY` | `0x11` (17) | *none* | `a b -> number` | Pops numbers `b` and `a`. Pushes $a \times b$. |
| `OP_DIVIDE` | `0x12` (18) | *none* | `a b -> number` | Pops numbers `b` and `a`. Pushes $a / b$ (IEEE 754 division). |
| `OP_NEGATE` | `0x14` (20) | *none* | `n -> -n` | Pops number `n`. Pushes $-n$. Runtime error if $n$ is not a number. |

---

### 3.6 I/O Operations

| Opcode | Hex / Dec | Operands | Stack Effect | Description |
| :--- | :---: | :--- | :---: | :--- |
| `OP_PRINT` | `0x15` (21) | *none* | `v ->` | Pops `v`, prints its formatted representation to `stdout`, followed by a newline. |

---

### 3.7 Control Flow & Jumps

Multi-byte offsets are encoded in **big-endian** order (`code[0] << 8 | code[1]`).

| Opcode | Hex / Dec | Operands | Stack Effect | Description |
| :--- | :---: | :--- | :---: | :--- |
| `OP_JUMP` | `0x16` (22) | `i16 offset` | `--` | Unconditional forward jump. Advances `ip` forward by `offset` bytes (`ip += offset`). |
| `OP_JUMP_IF_FALSE` | `0x17` (23) | `i16 offset` | `cond ->` | Evaluates condition on top of stack. If `IS_FALSY(cond)`, advances `ip` forward by `offset` bytes. |
| `OP_LOOP` | `0x18` (24) | `u16 magnitude` | `--` | Backward jump for loops. Decrements `ip` backward by `magnitude` bytes (`ip -= magnitude`). |

#### Jump Computation Details
```c
// Forward jump (OP_JUMP, OP_JUMP_IF_FALSE)
uint16_t offset = (uint16_t)((code[offset + 1] << 8) | code[offset + 2]);
target_ip = current_ip + 3 + offset;

// Backward loop (OP_LOOP)
uint16_t magnitude = (uint16_t)((code[offset + 1] << 8) | code[offset + 2]);
target_ip = current_ip + 3 - magnitude;
```

---

### 3.8 Functions & Closures

Call frames and closures manage activation records and lexical scoping across Milestone 3.

| Opcode | Hex / Dec | Operands | Stack Effect | Description |
| :--- | :---: | :--- | :---: | :--- |
| `OP_CALL` | `0x19` (25) | `u8 arg_count` | `fn args... -> result` | Invokes callable object with `arg_count` arguments. Sets up a new `CallFrame`. |
| `OP_CLOSURE` | `0x1A` (26) | `u8 const_idx` + descriptors | `-> closure` | Instantiates a closure wrapping the `ObjFunction` at `const_idx`. Consumes $N$ 2-byte upvalue descriptors (`u8 isLocal`, `u8 index`). |
| `OP_CLOSE_UPVALUE` | `0x1B` (27) | *none* | `v ->` | Closes an open upvalue pointing to a local on the stack, hoisting its value to the heap as the local goes out of scope. |
| `OP_RETURN` | `0x1C` (28) | *none* | `v ->` | Exits the current function, pops the call frame, and restores caller state, leaving return value on top of stack. |

---

## 4. Disassembler Output Format

The disassembler ([`src/debug.c`](file:///Users/aritra/Code/Languages/C/Project-1/src/debug.c)) provides visual inspection of compiled chunks:

```text
== demo chunk ==
0000  123 OP_CONSTANT         0 '1.2'
0002    | OP_CONSTANT         1 '3.4'
0004    | OP_ADD
0005    | OP_NEGATE
0006    | OP_PRINT
0007    | OP_RETURN
```

### Columns
1. **Byte Offset (`0000`)**: 4-digit hexadecimal/decimal offset of the instruction within the bytecode array.
2. **Line Number (`123` or `|`)**: Source file line where the bytecode originated. Repeated lines are shown as `|`.
3. **Opcode Name (`OP_CONSTANT`)**: Mnemonic identifying the operation.
4. **Operands (`0 '1.2'`)**: Decoded constant pool index, slot index, or jump target arrow (`0010 -> 0024`).
