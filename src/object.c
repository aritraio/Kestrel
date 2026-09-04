#include <stdio.h>

#include "object.h"

/* String allocation and GC arrive in Milestones 2 and 4. */
void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
  }
}