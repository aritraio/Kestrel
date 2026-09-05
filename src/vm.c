#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

/*
 * A single global VM keeps the design simple: the GC (Milestone 4) needs to
 * reach all roots from one place, and a global makes that trivial. If you
 * later want multiple independent VMs, thread a VM* through everything.
 */
VM vm;

static void resetStack(void) {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

void initVM(void) {
  resetStack();
  vm.objects = NULL;
  vm.globals = NULL;
  vm.globalCount = 0;
  vm.globalCapacity = 0;
}

void freeVM(void) {
  FREE_ARRAY(Global, vm.globals, vm.globalCapacity);
  vm.globals = NULL;
  vm.globalCount = 0;
  vm.globalCapacity = 0;
  freeObjects();
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
 * Report a runtime error with a stack trace: the formatted message, then one
 * `[line N] in fn()` per active frame (script prints as `script`), then
 * reset the stack so the caller can unwind cleanly.
 */
static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->closure->function;
    int instruction =
        (int)(frame->ip - function->chunk.code - 1);
    int line = -1;
    if (instruction >= 0 && instruction < function->chunk.count) {
      line = function->chunk.lines[instruction];
    }
    if (line >= 0) {
      fprintf(stderr, "[line %d] in ", line);
    } else {
      fprintf(stderr, "[unknown line] in ");
    }
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  resetStack();
}

/* Linear globals (M2). M4 replaces this with a hash table; the bytecode
 * stays identical. Returns the globals index or -1 if absent. */
static int findGlobal(ObjString* name) {
  for (int i = 0; i < vm.globalCount; i++) {
    ObjString* candidate = vm.globals[i].name;
    if (candidate->length == name->length &&
        memcmp(candidate->chars, name->chars, (size_t)name->length) == 0) {
      return i;
    }
  }
  return -1;
}

static void defineGlobal(ObjString* name, Value value) {
  int index = findGlobal(name);
  if (index != -1) {
    vm.globals[index].value = value;
    return;
  }

  if (vm.globalCapacity < vm.globalCount + 1) {
    int oldCapacity = vm.globalCapacity;
    vm.globalCapacity = GROW_CAPACITY(oldCapacity);
    vm.globals =
        GROW_ARRAY(Global, vm.globals, oldCapacity, vm.globalCapacity);
  }
  vm.globals[vm.globalCount].name = name;
  vm.globals[vm.globalCount].value = value;
  vm.globalCount++;
}

static void concatenate(void) {
  ObjString* b = AS_STRING(peek(0));
  ObjString* a = AS_STRING(peek(1));

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, (size_t)a->length);
  memcpy(chars + a->length, b->chars, (size_t)b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  pop();
  pop();
  push(OBJ_VAL(result));
}

static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue* createdValue = newUpvalue(local);
  createdValue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdValue;
  } else {
    prevUpvalue->next = createdValue;
  }
  return createdValue;
}

static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static bool call(ObjClosure* closure, int argCount) {
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.",
                 closure->function->arity, argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), argCount);
      default:
        break;
    }
  }
  runtimeError("Can only call functions and classes.");
  return false;
}

/*
 * The run loop (Milestone 3). Every instruction decodes from the current
 * frame (`frame->ip`, `frame->closure->function->chunk`); OP_CALL pushes a
 * new frame, OP_RETURN pops it, OP_CLOSURE materializes captured upvalues.
 */
static InterpretResult run(void) {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
  (frame->ip += 2, \
   (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define BINARY_OP(valueType, op)                                       \
  do {                                                                 \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                  \
      runtimeError("Operands must be numbers.");                       \
      return INTERPRET_RUNTIME_ERROR;                                  \
    }                                                                  \
    double b = AS_NUMBER(pop());                                       \
    double a = AS_NUMBER(pop());                                       \
    push(valueType(a op b));                                           \
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
    disassembleInstruction(&frame->closure->function->chunk,
                           (int)(frame->ip -
                                 frame->closure->function->chunk.code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        uint8_t index = READ_BYTE();
        Chunk* chunk = &frame->closure->function->chunk;
        if (index >= chunk->constants.count) {
          runtimeError("Constant index %d out of bounds.", index);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(chunk->constants.values[index]);
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
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }
      case OP_GET_GLOBAL: {
        uint8_t nameIndex = READ_BYTE();
        Chunk* chunk = &frame->closure->function->chunk;
        if (nameIndex >= chunk->constants.count) {
          runtimeError("Constant index %d out of bounds.", nameIndex);
          return INTERPRET_RUNTIME_ERROR;
        }
        Value nameVal = chunk->constants.values[nameIndex];
        if (!IS_STRING(nameVal)) {
          runtimeError("Global name must be a string.");
          return INTERPRET_RUNTIME_ERROR;
        }
        int index = findGlobal(AS_STRING(nameVal));
        if (index == -1) {
          runtimeError("Undefined variable '%s'.", AS_CSTRING(nameVal));
          return INTERPRET_RUNTIME_ERROR;
        }
        push(vm.globals[index].value);
        break;
      }
      case OP_DEFINE_GLOBAL: {
        uint8_t nameIndex = READ_BYTE();
        Chunk* chunk = &frame->closure->function->chunk;
        if (nameIndex >= chunk->constants.count) {
          runtimeError("Constant index %d out of bounds.", nameIndex);
          return INTERPRET_RUNTIME_ERROR;
        }
        Value nameVal = chunk->constants.values[nameIndex];
        if (!IS_STRING(nameVal)) {
          runtimeError("Global name must be a string.");
          return INTERPRET_RUNTIME_ERROR;
        }
        defineGlobal(AS_STRING(nameVal), peek(0));
        pop();
        break;
      }
      case OP_SET_GLOBAL: {
        uint8_t nameIndex = READ_BYTE();
        Chunk* chunk = &frame->closure->function->chunk;
        if (nameIndex >= chunk->constants.count) {
          runtimeError("Constant index %d out of bounds.", nameIndex);
          return INTERPRET_RUNTIME_ERROR;
        }
        Value nameVal = chunk->constants.values[nameIndex];
        if (!IS_STRING(nameVal)) {
          runtimeError("Global name must be a string.");
          return INTERPRET_RUNTIME_ERROR;
        }
        int index = findGlobal(AS_STRING(nameVal));
        if (index == -1) {
          runtimeError("Undefined variable '%s'.", AS_CSTRING(nameVal));
          return INTERPRET_RUNTIME_ERROR;
        }
        vm.globals[index].value = peek(0);
        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(0);
        break;
      }
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER:
        BINARY_OP(BOOL_VAL, >);
        break;
      case OP_LESS:
        BINARY_OP(BOOL_VAL, <);
        break;
      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUBTRACT:
        BINARY_OP(NUMBER_VAL, -);
        break;
      case OP_MULTIPLY:
        BINARY_OP(NUMBER_VAL, *);
        break;
      case OP_DIVIDE:
        BINARY_OP(NUMBER_VAL, /);
        break;
      case OP_NOT: {
        Value value = pop();
        push(BOOL_VAL(IS_FALSY(value)));
        break;
      }
      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      case OP_PRINT:
        printValue(pop());
        printf("\n");
        break;
      case OP_JUMP: {
        Chunk* chunk = &frame->closure->function->chunk;
        if (frame->ip + 2 > chunk->code + chunk->count) {
          runtimeError("Jump past end of chunk.");
          return INTERPRET_RUNTIME_ERROR;
        }
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        Chunk* chunk = &frame->closure->function->chunk;
        if (frame->ip + 2 > chunk->code + chunk->count) {
          runtimeError("Jump past end of chunk.");
          return INTERPRET_RUNTIME_ERROR;
        }
        uint16_t offset = READ_SHORT();
        /* Peek: the condition stays for the compiler-emitted OP_POP so
         * `and`/`or` can leave the short-circuited value on the stack. */
        if (IS_FALSY(peek(0))) frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        Chunk* chunk = &frame->closure->function->chunk;
        if (frame->ip + 2 > chunk->code + chunk->count) {
          runtimeError("Jump past end of chunk.");
          return INTERPRET_RUNTIME_ERROR;
        }
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case OP_CALL: {
        int argCount = READ_BYTE();
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_CLOSURE: {
        Chunk* chunk = &frame->closure->function->chunk;
        if (frame->ip >= chunk->code + chunk->count) {
          runtimeError("Truncated closure instruction.");
          return INTERPRET_RUNTIME_ERROR;
        }
        uint8_t constantIndex = READ_BYTE();
        if (constantIndex >= chunk->constants.count) {
          runtimeError("Constant index %d out of bounds.", constantIndex);
          return INTERPRET_RUNTIME_ERROR;
        }
        Value functionVal = chunk->constants.values[constantIndex];
        if (!IS_FUNCTION(functionVal)) {
          runtimeError("Closure constant must be a function.");
          return INTERPRET_RUNTIME_ERROR;
        }
        ObjFunction* function = AS_FUNCTION(functionVal);
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));
        for (int i = 0; i < function->upvalueCount; i++) {
          if (frame->ip + 2 > chunk->code + chunk->count) {
            runtimeError("Truncated closure upvalue.");
            return INTERPRET_RUNTIME_ERROR;
          }
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (isLocal != 0) {
            closure->upvalues[i] = captureUpvalue(frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }
      case OP_CLOSE_UPVALUE:
        closeUpvalues(vm.stackTop - 1);
        pop();
        break;
      case OP_RETURN: {
        Value result = pop();
        closeUpvalues(frame->slots);
        vm.frameCount--;
        if (vm.frameCount == 0) {
          pop();
          return INTERPRET_OK;
        }
        vm.stackTop = frame->slots;
        push(result);
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      default:
        runtimeError("Unknown opcode %d.", instruction);
        return INTERPRET_RUNTIME_ERROR;
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
  ObjFunction* function = compile(source);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));
  ObjClosure* closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0);

  return run();
}

InterpretResult interpretChunk(Chunk* chunk) {
  /* Demo path: wrap a hand-assembled chunk in a script function/closure.
   * Takes ownership of the chunk's buffers (caller must not freeChunk). */
  ObjFunction* function = newFunction();
  freeChunk(&function->chunk);
  function->chunk = *chunk;

  push(OBJ_VAL(function));
  ObjClosure* closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0);

  InterpretResult result = run();

  /* run() popped the closure; the function (and its chunk buffers, now
   * owned by the function) will be reclaimed by freeObjects in freeVM. */
  return result;
}
