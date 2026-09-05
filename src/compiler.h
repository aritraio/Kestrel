#ifndef COMPILER_H
#define COMPILER_H

#include "object.h"

/*
 * Single-pass Pratt parser compiler (Milestones 2-3).
 *
 * Compiles source text into an ObjFunction (top-level script). Returns NULL
 * if any compile error was reported. Diagnostics go to stderr with line
 * numbers; the caller maps NULL to exit code 65 (EX_DATAERR).
 */
ObjFunction* compile(const char* source);

#endif
