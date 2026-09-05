#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"
#include "value.h"

/*
 * All dynamic allocation goes through reallocate(). It tracks bytesAllocated
 * and runs the mark-and-sweep collector (Milestone 4) before handing out
 * memory when the threshold is reached. Keep every allocation on this road.
 */

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
  (type*)reallocate(pointer, sizeof(type) * (size_t)(oldCount), sizeof(type) * (size_t)(newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
  reallocate(pointer, sizeof(type) * (size_t)(oldCount), 0)

#define ALLOCATE(type, count) \
  (type*)reallocate(NULL, 0, sizeof(type) * (size_t)(count))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);
void markObject(Obj* object);
void markValue(Value value);
void collectGarbage(void);

#endif