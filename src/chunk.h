#ifndef CHUNK_H
#define CHUNK_H

#include "common.h"
#include "value.h"

/*
 * ============================= THE INSTRUCTION SET =============================
 *
 * Encoding rules:
 *   - Every instruction is one opcode byte followed by fixed operands.
 *   - Multi-byte operands are big-endian (high byte first).
 *   - Constant-pool indexes are 1 byte  -> max 256 constants per chunk.
 *   - Jump operands are 2 bytes: OP_JUMP and OP_JUMP_IF_FALSE take a signed
 *     offset; OP_LOOP takes an unsigned offset magnitude and jumps backward.
 *   - Local slot / upvalue / arg-count operands are 1 byte.
 *
 * Stack effect notation: "a b -> c" means pop b, then a; push c.
 *
 * Opcode               Operand(s)            Stack effect        Purpose
 * ---------------------------------------------------------------------------
 * OP_CONSTANT          u8 constant index     -> v                push from pool
 * OP_NIL / TRUE/FALSE  --                    -> v                push literal
 * OP_POP               --                    v ->               discard top
 * OP_GET_LOCAL         u8 slot               -> v                read local
 * OP_SET_LOCAL         u8 slot               v -> v              write local
 * OP_GET_GLOBAL        u8 name const idx     -> v                read global
 * OP_DEFINE_GLOBAL     u8 name const idx     v ->                declare global
 * OP_SET_GLOBAL        u8 name const idx     v -> v              write global
 * OP_GET_UPVALUE       u8 upvalue index      -> v                read captured var
 * OP_SET_UPVALUE       u8 upvalue index      v -> v              write captured var
 * OP_EQUAL             --                    a b -> bool
 * OP_GREATER           --                    a b -> bool
 * OP_LESS              --                    a b -> bool
 * OP_ADD/SUB/MUL/DIV   --                    a b -> number
 * OP_NOT               --                    v -> bool
 * OP_NEGATE            --                    n -> -n
 * OP_PRINT             --                    v ->               pop and print
 * OP_JUMP              i16 offset            --                 unconditional jump
 * OP_JUMP_IF_FALSE     i16 offset            cond -> cond       peek; branch if
 *                                                        falsy (compiler emits
 *                                                        OP_POP per branch)
 * OP_LOOP              u16 offset magnitude   --                 jump backward by
 *                                                              (ip - offset)
 * OP_CALL              u8 arg count          fn args... -> result
 * OP_CLOSURE           u8 const idx +        -> closure         build a closure from
 *                      upvalue descriptor                      the function constant;
 *                      pairs (2 bytes each):                   each descriptor is a
 *                      u8 isLocal, u8 index                    2-byte pair: 1 byte
 *                                                              isLocal flag, 1 byte
 *                                                              captured index
 * OP_CLOSE_UPVALUE     --                    v ->               end captured local
 * OP_RETURN            --                    v ->               return top of stack
 * ---------------------------------------------------------------------------
 *
 * That is the whole ISA: 24 opcodes. Everything the language does (variables,
 * if/else, while, functions, closures) compiles down to combinations of these.
 */
typedef enum {
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_GLOBAL,
  OP_DEFINE_GLOBAL,
  OP_SET_GLOBAL,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
  OP_NEGATE,
  OP_PRINT,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_RETURN
} OpCode;

/*
 * A chunk is one function's compiled bytecode: the encoded instructions, a
 * parallel array of source line numbers (for the disassembler and error
 * messages), and the constant pool it references.
 */
typedef struct {
  int count;
  int capacity;
  uint8_t* code;
  int* lines;
  ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);

/* Returns the index of the newly appended constant. */
int addConstant(Chunk* chunk, Value value);

#endif