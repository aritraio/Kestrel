#ifndef VM_H
#define VM_H

#include "chunk.h"
#include "object.h"
#include "value.h"

#define STACK_MAX 256

/*
 * The VM. Milestone 1 runs a minimal subset of the ISA; the struct already
 * carries the fields later milestones add around them:
 *
 *   Milestone 3: CallFrame frames[FRAMES_MAX] -- function call stack;
 *                Table globals                 -- global variable table;
 *                Obj* objects, Obj** grayStack -- GC bookkeeping (M4);
 *                ObjUpvalue* openUpvalues      -- closure capture chain.
 *
 * The value stack doubles as both the operand stack and local-variable
 * storage: locals live at fixed slots below the current frame's base.
 */
typedef struct {
  Chunk* chunk;
  uint8_t* ip; /* instruction pointer */

  Value stack[STACK_MAX];
  Value* stackTop; /* points one past the topmost value */

  /* Milestone 3+: call frames, globals table, open upvalues, object list. */
} VM;

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
InterpretResult interpret(Chunk* chunk);

#endif