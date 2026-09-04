#ifndef VALUE_H
#define VALUE_H

#include "common.h"

/*
 * Every value in the language is one of these. The `type` tag tells you which
 * member of the union is live. We use a tagged union first because it is easy
 * to debug and read; NaN-boxing (packing the tag into an IEEE-754 quiet-NaN
 * payload) is a pure drop-in optimization you can do later -- the bytecode
 * format and every IS_/AS_ accessor macro stay the same.
 */
typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ /* heap-allocated object: string, function, closure, ... */
} ValueType;

typedef struct sObj Obj;
typedef struct sObjString ObjString;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj* obj;
  } as;
} Value;

#define IS_BOOL(value)   ((value).type == VAL_BOOL)
#define IS_NIL(value)    ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value)    ((value).type == VAL_OBJ)

#define AS_BOOL(value)   ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value)    ((value).as.obj)

#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = (value)}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = (value)}})
#define OBJ_VAL(object)   ((Value){VAL_OBJ, {.obj = (Obj*)(object)}})

/*
 * Truthiness rule (deliberate, Python-like-but-simpler): only `false` and
 * `nil` are falsy. Numbers -- including 0.0 -- and strings are always truthy.
 * This avoids surprising implicit conversions in `if` and `while`.
 */
#define IS_FALSY(value) \
  (IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value)))

typedef struct {
  int capacity;
  int count;
  Value* values;
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);
bool valuesEqual(Value a, Value b);

#endif