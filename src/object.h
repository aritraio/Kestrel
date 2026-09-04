#ifndef OBJECT_H
#define OBJECT_H

#include "common.h"
#include "value.h"

/*
 * Every heap-allocated object starts with this header. `next` threads all
 * objects into one singly-linked list owned by the VM; Milestone 4's GC walks
 * that list to mark and sweep. `isMarked` is the mark bit.
 */
typedef enum {
  OBJ_STRING
} ObjType;

struct sObj {
  ObjType type;
  bool isMarked;
  struct sObj* next;
};

struct sObjString {
  Obj obj;
  int length;
  char chars[]; /* flexible array member: the string data lives inline */
};

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

/* String allocation arrives with the compiler (Milestone 2). */
void printObject(Value value);

#endif