#ifndef DEBUG_H
#define DEBUG_H

#include "chunk.h"

/* Disassembles every instruction in the chunk with source line numbers. */
void disassembleChunk(Chunk* chunk, const char* name);

/* Disassembles the instruction at `offset`; returns the offset of the next one. */
int disassembleInstruction(Chunk* chunk, int offset);

#endif