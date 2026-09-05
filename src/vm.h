#ifndef VM_H
#define VM_H

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

/*
 * The VM (Milestone 4: functions + closures + GC).
 *
 * Call frames give each function its own `ip` and window (`slots`) into the
 * shared value stack. `openUpvalues` threads heap upvalues that still point
 * into the stack. `globals` and `strings` (intern pool) are hash tables.
 * `objects` is the intrusive list of all heap objects; `grayStack` is the
 * GC worklist; `bytesAllocated`/`nextGC` drive collection in reallocate().
 */
typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;

  Value stack[STACK_MAX];
  Value* stackTop; /* points one past the topmost value */

  Obj* objects; /* intrusive list of all heap objects (see object.c) */

  Table globals; /* global variables by interned name */
  Table strings; /* interned strings (weak: cleaned via tableRemoveWhite) */

  ObjUpvalue* openUpvalues;

  size_t bytesAllocated;
  size_t nextGC;
  Obj** grayStack;
  int grayCount;
  int grayCapacity;
} VM;

extern VM vm;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

/* Single global VM instance (see vm.c) -- these take no VM argument. */
void initVM(void);
void freeVM(void);
void push(Value value);
Value pop(void);
/* Compiles and runs `source`; maps to EX_DATAERR (65) / EX_SOFTWARE paths. */
InterpretResult interpret(const char* source);
/* Runs an already-assembled chunk (demo / tests). Takes ownership. */
InterpretResult interpretChunk(Chunk* chunk);

#endif
