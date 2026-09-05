#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Debug switches -- flip these on while developing a milestone.
 *
 * DEBUG_TRACE_EXECUTION dumps the VM stack before every instruction and then
 * disassembles that instruction. It is your window into the runtime.
 * DEBUG_STRESS_GC runs the collector on every allocation (slow, thorough).
 * DEBUG_LOG_GC prints each GC phase (mark/sweep timing, bytes reclaimed).
 */
// #define DEBUG_TRACE_EXECUTION
// #define DEBUG_STRESS_GC
// #define DEBUG_LOG_GC

#define UINT8_COUNT (UINT8_MAX + 1)

#endif