#ifndef VM_H
#define VM_H

#include "chunk.h"
#include "object.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

/*
 * The VM (Milestone 3: functions + closures).
 *
 * Call frames give each function its own `ip` and window (`slots`) into the
 * shared value stack. Locals are frame-relative (`frame->slots[slot]`).
 * `openUpvalues` threads heap upvalues that still point into the stack;
 * OP_CLOSE_UPVALUE hoists them to the heap when their local goes out of
 * scope.
 *
 * Globals (M2) stay a linear name/value list; M4 swaps in a hash table
 * without changing bytecode. `objects` is the GC root list (M4).
 */
typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct {
  ObjString* name;
  Value value;
} Global;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;

  Value stack[STACK_MAX];
  Value* stackTop; /* points one past the topmost value */

  Obj* objects; /* intrusive list of all heap objects (see object.c) */

  Global* globals; /* linear globals table (hash table in M4) */
  int globalCount;
  int globalCapacity;

  ObjUpvalue* openUpvalues;
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
