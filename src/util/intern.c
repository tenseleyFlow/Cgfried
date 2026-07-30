#include "util/intern.h"

#include <stdlib.h>
#include <string.h>

#include "diag.h"

void intern_init(Interner *in, Arena *arena)
{
    strmap_init(&in->map);
    in->strs = NULL;
    in->len = 0;
    in->cap = 0;
    in->arena = arena;
    /* Reserve id 0 as invalid. */
    if (in->cap == 0) {
        in->cap = 64;
        in->strs = cgf_xmalloc(in->cap * sizeof(const char *));
    }
    in->strs[0] = NULL;
    in->len = 1;
}

u32 intern(Interner *in, const char *s, size_t len)
{
    void *hit = strmap_get(&in->map, s, len);
    const char *copy;

    if (hit)
        return (u32)(uintptr_t)hit;

    if (in->len > 0xFFFFFFFFu)
        CGF_ICE("interner overflow: more than 2^32 distinct strings");
    if (in->len == in->cap) {
        in->cap *= 2;
        in->strs = cgf_xrealloc(in->strs, in->cap * sizeof(const char *));
    }
    copy = arena_strndup(in->arena, s, len);
    in->strs[in->len] = copy;
    strmap_put(&in->map, s, len, (void *)(uintptr_t)in->len);
    return (u32)in->len++;
}

u32 intern_cstr(Interner *in, const char *s)
{
    return intern(in, s, strlen(s));
}

const char *intern_str(const Interner *in, u32 id)
{
    if (id == 0 || id >= in->len)
        CGF_ICE("intern_str: bad id %u (have %zu)", (unsigned)id, in->len);
    return in->strs[id];
}

size_t intern_count(const Interner *in)
{
    return in->len - 1;
}

void intern_free(Interner *in)
{
    strmap_free(&in->map);
    free(in->strs);
    in->strs = NULL;
    in->len = 0;
    in->cap = 0;
}
