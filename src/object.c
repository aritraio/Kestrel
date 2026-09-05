#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

/* FNV-1a 32-bit hash; stored on every string, used for interning. */
uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619u;
  }
  return hash;
}

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->isMarked = false;
  object->next = vm.objects;
  vm.objects = object;
  return object;
}

ObjFunction* newFunction(void) {
  ObjFunction* function = (ObjFunction*)allocateObject(sizeof(ObjFunction),
                                                       OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  initChunk(&function->chunk);
  return function;
}

ObjClosure* newClosure(ObjFunction* function) {
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }

  ObjClosure* closure =
      (ObjClosure*)allocateObject(sizeof(ObjClosure), OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjUpvalue* newUpvalue(Value* slot) {
  ObjUpvalue* upvalue =
      (ObjUpvalue*)allocateObject(sizeof(ObjUpvalue), OBJ_UPVALUE);
  upvalue->location = slot;
  upvalue->closed = NIL_VAL;
  upvalue->next = NULL;
  return upvalue;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
  ObjString* string =
      (ObjString*)allocateObject(sizeof(ObjString) + (size_t)length + 1,
                                 OBJ_STRING);
  string->length = length;
  string->hash = hash;
  memcpy(string->chars, chars, (size_t)length);
  string->chars[length] = '\0';
  /* Intern: keep the string reachable across tableSet's own allocation
   * (which may trigger a collection) by rooting it on the VM stack. */
  push(OBJ_VAL(string));
  tableSet(&vm.strings, string, NIL_VAL);
  pop();
  return string;
}

ObjString* copyString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned =
      tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) return interned;
  return allocateString((char*)chars, length, hash);
}

ObjString* takeString(char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned =
      tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }
  ObjString* string = allocateString(chars, length, hash);
  FREE_ARRAY(char, chars, length + 1);
  return string;
}

void freeObject(Obj* object) {
  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
      reallocate(object, sizeof(ObjClosure), 0);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      reallocate(object, sizeof(ObjFunction), 0);
      break;
    }
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      size_t size = sizeof(ObjString) + (size_t)string->length + 1;
      reallocate(object, size, 0);
      break;
    }
    case OBJ_UPVALUE:
      reallocate(object, sizeof(ObjUpvalue), 0);
      break;
  }
}

void freeObjects(void) {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
  vm.objects = NULL;
}

static void printFunction(ObjFunction* function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_CLOSURE:
      printFunction(AS_CLOSURE(value)->function);
      break;
    case OBJ_FUNCTION:
      printFunction(AS_FUNCTION(value));
      break;
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
    case OBJ_UPVALUE:
      printf("upvalue");
      break;
  }
}
