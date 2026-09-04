#include <stdlib.h>

#include "memory.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  (void)oldSize; /* unused until the GC exists (Milestone 4) */

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);
  if (result == NULL) {
    fprintf(stderr, "Out of memory.\n");
    exit(1);
  }
  return result;
}