#include <stdio.h>

#include "debug.h"
#include "object.h"
#include "value.h"

/*
 * The disassembler is your debugger until the VM grows error reporting. It
 * prints, per instruction: byte offset, source line (or "|" when the line is
 * unchanged), opcode name, and decoded operands. Disassembly of the whole ISA
 * is implemented now even though the compiler only emits a subset until
 * Milestone 3.
 *
 * Robustness: every operand decode is bounds-checked against chunk->count so
 * a truncated or malformed chunk can never read past the bytecode buffer, and
 * constant indexes are validated against the pool before they are used.
 */
void disassembleChunk(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset);
  }
}

static int simpleInstruction(const char* name, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

/*
 * Multi-byte operands are big-endian: high byte first. Each decoder below
 * first verifies that the whole operand is present in the chunk; a truncated
 * instruction prints a marker and ends the disassembly.
 */

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
  if (offset + 2 > chunk->count) {
    printf("%-16s <truncated instruction>\n", name);
    return chunk->count;
  }

  uint8_t constant = chunk->code[offset + 1];
  printf("%-16s %4d '", name, constant);
  if (constant < chunk->constants.count) {
    printValue(chunk->constants.values[constant]);
  } else {
    printf("<constant out of bounds>");
  }
  printf("'\n");
  return offset + 2;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
  if (offset + 2 > chunk->count) {
    printf("%-16s <truncated instruction>\n", name);
    return chunk->count;
  }

  uint8_t slot = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, slot);
  return offset + 2;
}

/* `sign` is +1 for forward jumps, -1 for OP_LOOP (see chunk.h). */
static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset) {
  if (offset + 3 > chunk->count) {
    printf("%-16s <truncated instruction>\n", name);
    return chunk->count;
  }

  uint16_t jump = (uint16_t)((chunk->code[offset + 1] << 8) |
                             chunk->code[offset + 2]);
  printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
  return offset + 3;
}

static int closureInstruction(Chunk* chunk, int offset) {
  int start = offset;
  if (offset + 2 > chunk->count) {
    printf("%-16s <truncated instruction>\n", "OP_CLOSURE");
    return chunk->count;
  }

  offset++; /* opcode */
  uint8_t constant = chunk->code[offset++];
  printf("%-16s %4d ", "OP_CLOSURE", constant);
  if (constant < chunk->constants.count) {
    printValue(chunk->constants.values[constant]);
  } else {
    printf("<constant out of bounds>");
  }
  printf("\n");

  /* Each upvalue is a 2-byte descriptor (isLocal, index). The count lives
   * inside the ObjFunction constant; without it we cannot know how many
   * pairs follow, so a non-function constant ends the decode here. */
  if (constant >= chunk->constants.count) return offset;
  Value functionVal = chunk->constants.values[constant];
  if (!IS_FUNCTION(functionVal)) return offset;
  int upvalueCount = AS_FUNCTION(functionVal)->upvalueCount;

  for (int j = 0; j < upvalueCount; j++) {
    if (offset + 2 > chunk->count) {
      printf("%04d      |                     <truncated upvalue>\n", offset);
      return chunk->count;
    }
    uint8_t isLocal = chunk->code[offset++];
    uint8_t index = chunk->code[offset++];
    printf("%04d      |                     %s %d\n", start,
           isLocal != 0 ? "local" : "upvalue", index);
  }
  return offset;
}

int disassembleInstruction(Chunk* chunk, int offset) {
  if (offset < 0 || offset >= chunk->count) return chunk->count;

  printf("%04d ", offset);

  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    printf("   | ");
  } else {
    printf("%4d ", chunk->lines[offset]);
  }

  uint8_t instruction = chunk->code[offset];
  switch (instruction) {
    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
    case OP_NIL:
      return simpleInstruction("OP_NIL", offset);
    case OP_TRUE:
      return simpleInstruction("OP_TRUE", offset);
    case OP_FALSE:
      return simpleInstruction("OP_FALSE", offset);
    case OP_POP:
      return simpleInstruction("OP_POP", offset);
    case OP_GET_LOCAL:
      return byteInstruction("OP_GET_LOCAL", chunk, offset);
    case OP_SET_LOCAL:
      return byteInstruction("OP_SET_LOCAL", chunk, offset);
    case OP_GET_GLOBAL:
      return constantInstruction("OP_GET_GLOBAL", chunk, offset);
    case OP_DEFINE_GLOBAL:
      return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL:
      return constantInstruction("OP_SET_GLOBAL", chunk, offset);
    case OP_GET_UPVALUE:
      return byteInstruction("OP_GET_UPVALUE", chunk, offset);
    case OP_SET_UPVALUE:
      return byteInstruction("OP_SET_UPVALUE", chunk, offset);
    case OP_EQUAL:
      return simpleInstruction("OP_EQUAL", offset);
    case OP_GREATER:
      return simpleInstruction("OP_GREATER", offset);
    case OP_LESS:
      return simpleInstruction("OP_LESS", offset);
    case OP_ADD:
      return simpleInstruction("OP_ADD", offset);
    case OP_SUBTRACT:
      return simpleInstruction("OP_SUBTRACT", offset);
    case OP_MULTIPLY:
      return simpleInstruction("OP_MULTIPLY", offset);
    case OP_DIVIDE:
      return simpleInstruction("OP_DIVIDE", offset);
    case OP_NOT:
      return simpleInstruction("OP_NOT", offset);
    case OP_NEGATE:
      return simpleInstruction("OP_NEGATE", offset);
    case OP_PRINT:
      return simpleInstruction("OP_PRINT", offset);
    case OP_JUMP:
      return jumpInstruction("OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
      return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_LOOP:
      return jumpInstruction("OP_LOOP", -1, chunk, offset);
    case OP_CALL:
      return byteInstruction("OP_CALL", chunk, offset);
    case OP_CLOSURE:
      return closureInstruction(chunk, offset);
    case OP_CLOSE_UPVALUE:
      return simpleInstruction("OP_CLOSE_UPVALUE", offset);
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}