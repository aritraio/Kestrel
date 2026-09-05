#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "debug.h"
#include "scanner.h"
#include "vm.h"

/*
 * Milestone 2 (Phase A) driver:
 *
 *   ./kestrel demo         Build a chunk by hand, disassemble it, run it.
 *   ./kestrel <file>       Compile <file> to bytecode and execute it.
 *                          Exit 65 (EX_DATAERR) on compile error,
 *                          70 (EX_SOFTWARE) on runtime error.
 *   ./kestrel --lex <file> Lex <file> and dump every token with its line;
 *                          exits 65 (EX_DATAERR) if the source has errors.
 */

static void usage(void) {
  fprintf(stderr,
          "Usage:\n"
          "  kestrel demo         build a hand-written chunk, disassemble and run it\n"
          "  kestrel <file>       compile <file> to bytecode and run it\n"
          "  kestrel --lex <file> lex <file> and print its tokens\n");
}

/* Hand-assembles: -(1.2 + 3.4), i.e. 1.2 3.4 ADD NEGATE. */
static void demoChunk(void) {
  Chunk chunk;
  initChunk(&chunk);

  int constant = addConstant(&chunk, NUMBER_VAL(1.2));
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, (uint8_t)constant, 123);

  constant = addConstant(&chunk, NUMBER_VAL(3.4));
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, (uint8_t)constant, 123);

  writeChunk(&chunk, OP_ADD, 123);
  writeChunk(&chunk, OP_NEGATE, 123);
  writeChunk(&chunk, OP_PRINT, 123);
  /* Script frames return a value: leave nil for OP_RETURN to consume,
   * mirroring what emitReturn() generates for compiled scripts. */
  writeChunk(&chunk, OP_NIL, 123);
  writeChunk(&chunk, OP_RETURN, 123);

  disassembleChunk(&chunk, "demo chunk");

  initVM();
  /* interpretChunk takes ownership of the chunk buffers; do not freeChunk
   * afterwards (the function object owns them until freeVM). */
  interpretChunk(&chunk);
  freeVM();
}

static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  long fileSize = ftell(file);
  /* ftell() fails on special files and pipes (returns -1); refuse rather
   * than allocating a huge buffer from a garbage size. */
  if (fileSize < 0) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }
  rewind(file);

  char* buffer = (char*)malloc((size_t)fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  size_t bytesRead = fread(buffer, sizeof(char), (size_t)fileSize, file);
  if (bytesRead < (size_t)fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }
  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}

/* Returns true if the source contained any lexical error. */
static bool lexFile(const char* path) {
  char* source = readFile(path);

  bool hadError = false;

  initScanner(source);
  for (;;) {
    Token token = scanToken();
    printf("line %-4d %-14s '%.*s'\n", token.line,
           tokenTypeName(token.type), token.length, token.start);

    if (token.type == TOKEN_EOF) break;

    if (token.type == TOKEN_ERROR) {
      fprintf(stderr, "Lex error: %.*s\n", token.length, token.start);
      hadError = true;
      break;
    }
  }

  free(source);
  return hadError;
}

static int runFile(const char* path) {
  char* source = readFile(path);

  initVM();
  InterpretResult result = interpret(source);
  freeVM();

  free(source);

  if (result == INTERPRET_COMPILE_ERROR) return 65;
  if (result == INTERPRET_RUNTIME_ERROR) return 70;
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc == 2 && strcmp(argv[1], "demo") == 0) {
    demoChunk();
    return 0;
  }

  if (argc == 3 && strcmp(argv[1], "--lex") == 0) {
    /* 65 = EX_DATAERR: malformed input data. */
    return lexFile(argv[2]) ? 65 : 0;
  }

  if (argc == 2) {
    return runFile(argv[1]);
  }

  usage();
  return 64;
}
