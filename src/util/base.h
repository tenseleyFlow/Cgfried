#ifndef CGF_UTIL_BASE_H
#define CGF_UTIL_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#define CGF_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* Allocation failure is an ICE (exit 4), never a NULL return: a compiler
 * that limps on after OOM produces wrong output instead of no output. */
void *cgf_xmalloc(size_t size);
void *cgf_xrealloc(void *p, size_t size);

#endif
