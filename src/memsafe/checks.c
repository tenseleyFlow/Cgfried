#include "memsafe/memsafe.h"

#include <string.h>

static const MsAllocFamily alloc_families[] = {
    {"malloc", true, MS_NO_ARG, MS_NO_ARG, false, true, false, false},
    {"calloc", true, MS_NO_ARG, MS_NO_ARG, false, true, true, true},
    {"realloc", true, MS_NO_ARG, 0, true, true, false, false},
    {"reallocarray", true, MS_NO_ARG, 0, true, true, false, false},
    {"free", false, MS_NO_ARG, 0, false, false, false, false},
    {"strdup", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true},
    {"strndup", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true},
    {"asprintf", true, 0, MS_NO_ARG, false, true, false, true},
    {"vasprintf", true, 0, MS_NO_ARG, false, true, false, true},
    {"aligned_alloc", true, MS_NO_ARG, MS_NO_ARG, false, true, false, false},
    {"posix_memalign", true, 0, MS_NO_ARG, false, true, false, false},
};

const MsAllocFamily *ms_alloc_family_lookup(const char *name)
{
    u32 i;

    if (!name)
        return NULL;
    for (i = 0; i < CGF_ARRAY_LEN(alloc_families); i++)
        if (strcmp(alloc_families[i].name, name) == 0)
            return &alloc_families[i];
    return NULL;
}

bool ms_alloc_seed_for_call(const IrModule *module, const IrInst *call,
                            AliasAllocSeed *out)
{
    const MsAllocFamily *family;

    if (!module || !call || !out || call->op != IR_CALL ||
        call->subop != FUNCREF_EXTERNAL || call->callee >= module->nsyms)
        return false;
    family = ms_alloc_family_lookup(module->syms[call->callee]);
    if (!family || !family->allocates)
        return false;
    out->call = call;
    out->owns_result =
        family->returns_ownership && family->alloc_out_arg == MS_NO_ARG;
    out->out_param = family->alloc_out_arg == MS_NO_ARG ? ALIAS_NO_OUT_PARAM
                                                        : family->alloc_out_arg;
    return true;
}

u32 ms_alias_alloc_seeds(Arena *arena, const IrModule *module,
                         const IrFunc *function, AliasAllocSeed **out)
{
    AliasAllocSeed *seeds;
    u32 count = 0;
    u32 bi;

    if (!out)
        return 0;
    *out = NULL;
    if (!arena || !module || !function)
        return 0;
    for (bi = 0; bi < function->nblocks; bi++) {
        const IrInst *in;

        for (in = function->blocks[bi].first; in; in = in->next) {
            AliasAllocSeed ignored;

            if (ms_alloc_seed_for_call(module, in, &ignored))
                count++;
        }
    }
    if (!count)
        return 0;
    seeds =
        arena_alloc(arena, count * sizeof(*seeds), _Alignof(AliasAllocSeed));
    count = 0;
    for (bi = 0; bi < function->nblocks; bi++) {
        const IrInst *in;

        for (in = function->blocks[bi].first; in; in = in->next)
            if (ms_alloc_seed_for_call(module, in, &seeds[count]))
                count++;
    }
    *out = seeds;
    return count;
}
