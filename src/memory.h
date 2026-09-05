#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"

/*
 * All dynamic allocation goes through reallocate(). Today it is a thin
 * wrapper over realloc(); in Milestone 4 (the GC) it becomes the single
 * allocation entry point where the collector can run before handing out
 * memory. Keep every allocation on this road.
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

#endif