#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "debug.h"
#include "vm.h"

/*
 * A single global VM keeps the design simple: the GC (Milestone 4) needs to
 * reach all roots from one place, and a global makes that trivial. If you
 * later want multiple independent VMs, thread a VM* through everything.
 */
VM vm;

static void resetStack(void) {
  vm.stackTop = vm.stack;
}

void initVM(void) {
  resetStack();
}

void freeVM(void) {
  /* Milestone 4: free the object list here. */
  resetStack(); /* leave the VM in a clean, reusable state */
}

/*
 * Stack discipline: push/pop are the only two entry points. The asserts keep
 * a corrupted stack from corrupting memory silently during development; they
 * compile away under NDEBUG, so they are debugging aids, not the runtime
 * safety net (which is the fixed-size array + type checks below).
 */
void push(Value value) {
  assert(vm.stackTop < vm.stack + STACK_MAX && "Stack overflow");
  *vm.stackTop++ = value;
}

Value pop(void) {
  assert(vm.stackTop > vm.stack && "Stack underflow");
  return *--vm.stackTop;
}

/* Inspect the value `distance` slots below the top without popping it. */
static Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}

/*
 * Report a runtime error: the formatted message, the source line at the
 * current instruction, then reset the stack so the caller can unwind cleanly
 * (the caller returns INTERPRET_RUNTIME_ERROR). assert() stays the last line
 * of defense; this is the friendly path for *typed* misuse of the stack.
 */
static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  int instruction = (int)(vm.ip - vm.chunk->code - 1);
  if (instruction >= 0 && instruction < vm.chunk->count) {
    fprintf(stderr, "[line %d] in script\n", vm.chunk->lines[instruction]);
  }

  resetStack();
}

/*
 * The run loop. This is a deliberately minimal dispatch loop covering the
 * value/arithmetic opcodes; Milestone 3 extends it with control flow, calls,
 * closures and globals. The instruction decode macros at the bottom are the
 * same pattern every later opcode will use.
 */
static InterpretResult run(void) {
#define READ_BYTE() (*vm.ip++)

#define BINARY_OP(op)                                                     \
  do {                                                                    \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                     \
      runtimeError("Operands must be numbers.");                          \
      return INTERPRET_RUNTIME_ERROR;                                     \
    }                                                                     \
    double b = AS_NUMBER(pop());                                          \
    double a = AS_NUMBER(pop());                                          \
    push(NUMBER_VAL(a op b));                                             \
  } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        /* Guard the pool index like the disassembler does, so a malformed
         * chunk cannot index past the constant array. */
        uint8_t index = READ_BYTE();
        if (index >= vm.chunk->constants.count) {
          runtimeError("Constant index %d out of bounds.", index);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(vm.chunk->constants.values[index]);
        break;
      }
      case OP_NIL:
        push(NIL_VAL);
        break;
      case OP_TRUE:
        push(BOOL_VAL(true));
        break;
      case OP_FALSE:
        push(BOOL_VAL(false));
        break;
      case OP_POP:
        pop();
        break;
      case OP_ADD:
        BINARY_OP(+);
        break;
      case OP_SUBTRACT:
        BINARY_OP(-);
        break;
      case OP_MULTIPLY:
        BINARY_OP(*);
        break;
      case OP_DIVIDE:
        BINARY_OP(/);
        break;
      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      case OP_NOT:
        push(BOOL_VAL(IS_FALSY(pop())));
        break;
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER:
        BINARY_OP(>);
        break;
      case OP_LESS:
        BINARY_OP(<);
        break;
      case OP_PRINT:
        printValue(pop());
        printf("\n");
        break;
      case OP_RETURN:
        return INTERPRET_OK;
    }
  }

#undef READ_BYTE
#undef BINARY_OP
}

InterpretResult interpret(Chunk* chunk) {
  vm.chunk = chunk;
  vm.ip = vm.chunk->code;
  return run();
}