#ifndef CGF_UTIL_INTERN_H
#define CGF_UTIL_INTERN_H

#include <stddef.h>

#include "util/arena.h"
#include "util/base.h"
#include "util/strmap.h"

/* String interner: stable u32 ids assigned in first-intern order (so ids are
 * as deterministic as the input), id 0 reserved as "invalid". Strings live in
 * the caller's arena and stay valid for its lifetime. */
typedef struct {
    Strmap map;        /* string -> id */
    const char **strs; /* id -> string; strs[0] == NULL */
    size_t len;
    size_t cap;
    Arena *arena;
} Interner;

void intern_init(Interner *in, Arena *arena);
u32 intern(Interner *in, const char *s, size_t len);
u32 intern_cstr(Interner *in, const char *s);
const char *intern_str(const Interner *in, u32 id);
size_t intern_count(const Interner *in); /* excludes the reserved id 0 */
void intern_free(Interner *in);          /* map only; strings are arena-owned */

#endif
