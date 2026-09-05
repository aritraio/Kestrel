#ifndef OBJECT_H
#define OBJECT_H

#include "chunk.h"
#include "common.h"
#include "value.h"

/*
 * Every heap-allocated object starts with this header. `next` threads all
 * objects into one singly-linked list owned by the VM; Milestone 4's GC walks
 * that list to mark and sweep. `isMarked` is the mark bit.
 */
typedef enum {
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_STRING,
  OBJ_UPVALUE
} ObjType;

struct sObj {
  ObjType type;
  bool isMarked;
  struct sObj* next;
};

typedef struct {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString* name; /* NULL for top-level script */
} ObjFunction;

struct sObjString {
  Obj obj;
  int length;
  uint32_t hash; /* FNV-1a; unused until interning (M4) but stored now */
  char chars[]; /* flexible array member: the string data lives inline */
};

typedef struct sObjUpvalue {
  Obj obj;
  Value* location; /* points into VM stack (open) or closed field */
  Value closed;    /* hoisted value once the local goes out of scope */
  struct sObjUpvalue* next;
} ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int upvalueCount;
} ObjClosure;

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

ObjFunction* newFunction(void);
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(Value* slot);
ObjString* copyString(const char* chars, int length);
ObjString* takeString(char* chars, int length);
uint32_t hashString(const char* key, int length);
void freeObjects(void);
void printObject(Value value);

#endif
