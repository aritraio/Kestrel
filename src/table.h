#ifndef TABLE_H
#define TABLE_H

#include "common.h"
#include "value.h"

/*
 * Open-addressing hash table with linear probing and tombstones (M4).
 * Used for globals (`vm.globals`) and string interning (`vm.strings`).
 * All entry storage goes through reallocate() so the GC can account it.
 */

typedef struct {
  ObjString* key;
  Value value;
} Entry;

typedef struct {
  int count;
  int capacity;
  Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash);
void tableRemoveWhite(Table* table);
void markTable(Table* table);

#endif
