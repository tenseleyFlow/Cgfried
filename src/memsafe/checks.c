#include "memsafe/memsafe.h"

#include <string.h>

static const MsAllocFamily alloc_families[] = {
    {"malloc", true, MS_NO_ARG, MS_NO_ARG, false, true, false, false,
     MS_ALLOC_SUCCESS_DIRECT, 0, MS_NO_ARG, false},
    {"calloc", true, MS_NO_ARG, MS_NO_ARG, false, true, true, true,
     MS_ALLOC_SUCCESS_DIRECT, 0, 1, false},
    {"realloc", true, MS_NO_ARG, 0, true, true, false, false,
     MS_ALLOC_SUCCESS_DIRECT, 1, MS_NO_ARG, false},
    {"reallocarray", true, MS_NO_ARG, 0, true, true, false, false,
     MS_ALLOC_SUCCESS_DIRECT, 1, 2, false},
    {"free", false, MS_NO_ARG, 0, false, false, false, false,
     MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, false},
    {"strdup", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
     MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, false},
    {"strndup", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
     MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, false},
    {"asprintf", true, 0, MS_NO_ARG, false, true, false, true,
     MS_ALLOC_SUCCESS_STATUS_NONNEG, MS_NO_ARG, MS_NO_ARG, false},
    {"vasprintf", true, 0, MS_NO_ARG, false, true, false, true,
     MS_ALLOC_SUCCESS_STATUS_NONNEG, MS_NO_ARG, MS_NO_ARG, false},
    {"aligned_alloc", true, MS_NO_ARG, MS_NO_ARG, false, true, false, false,
     MS_ALLOC_SUCCESS_DIRECT, 1, MS_NO_ARG, false},
    {"posix_memalign", true, 0, MS_NO_ARG, false, true, false, false,
     MS_ALLOC_SUCCESS_STATUS_ZERO, 2, MS_NO_ARG, false},
    {"fopen", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
     MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
    {"fdopen", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
     MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
    {"tmpfile", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
     MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
    {"popen", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
     MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
    {"freopen", true, MS_NO_ARG, 2, true, true, false, true,
     MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
    {"fclose", false, MS_NO_ARG, 0, false, false, false, false,
     MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
    {"pclose", false, MS_NO_ARG, 0, false, false, false, false,
     MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
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

/* A table row is a contract about a SHAPE, not merely about a name.  Both
 * tables selected rows by strcmp alone, so a call that merely forgot an
 * #include -- `strcpy(buf, "")` with no <string.h>, whose implicit
 * declaration returns int -- had the row's pointer facts attached to an
 * integer result, and the alias service's validator refused them as an ICE
 * on ordinary C.  gcc rejects the same mismatch by name
 * (-Wbuiltin-declaration-mismatch), and Sprint 39's format table already
 * selects rows on a compatible signature; these are that rule for the
 * memory tables.  On any disagreement the row does not apply and the callee
 * stays an unknown external, which is conservative by construction.
 * Found by the frontend fuzzer, seed 64271. */
static u32 ms_call_first_arg(const IrInst *call)
{
    return call->subop == FUNCREF_INDIRECT ? 1u : 0u;
}

static u32 ms_call_nargs(const IrInst *call)
{
    u32 first = ms_call_first_arg(call);

    return call->nops > first ? call->nops - first : 0u;
}

static bool ms_call_arg_is_ptr(const IrInst *call, u32 index)
{
    return index < ms_call_nargs(call) &&
           call->ops[ms_call_first_arg(call) + index].type == IRT_PTR;
}

/* An extent ordinal is read as an integer byte count. */
static bool ms_call_arg_is_scalar(const IrInst *call, u32 index)
{
    return index < ms_call_nargs(call) &&
           call->ops[ms_call_first_arg(call) + index].type != IRT_PTR;
}

static bool ms_call_returns_ptr(const IrInst *call)
{
    return call->result.v != 0 && call->type == IRT_PTR;
}

static bool alloc_family_fits_call(const MsAllocFamily *family,
                                   const IrInst *call)
{
    if (family->alloc_out_arg != MS_NO_ARG) {
        if (!ms_call_arg_is_ptr(call, family->alloc_out_arg))
            return false;
    } else if (family->allocates && family->returns_ownership &&
               !ms_call_returns_ptr(call)) {
        return false;
    }
    if (family->frees_arg != MS_NO_ARG &&
        !ms_call_arg_is_ptr(call, family->frees_arg))
        return false;
    if (family->size_arg != MS_NO_ARG &&
        !ms_call_arg_is_scalar(call, family->size_arg))
        return false;
    if (family->size_arg2 != MS_NO_ARG &&
        !ms_call_arg_is_scalar(call, family->size_arg2))
        return false;
    return true;
}

static bool lib_summary_fits_call(const MsLibSummary *lib, const IrInst *call)
{
    u64 ptr_args =
        lib->deref_mask | lib->write_mask | lib->escape_mask | lib->free_mask;
    u32 i;

    if ((lib->return_alias >= 0 || lib->returns_ownership) &&
        !ms_call_returns_ptr(call))
        return false;
    if (lib->return_alias >= 0)
        ptr_args |= 1ull << (u32)lib->return_alias;
    for (i = 0; i < 64; i++)
        if ((ptr_args & (1ull << i)) && !ms_call_arg_is_ptr(call, i))
            return false;
    if (lib->write_size_arg >= 0 &&
        !ms_call_arg_is_scalar(call, (u32)lib->write_size_arg))
        return false;
    if (lib->write_size_arg2 >= 0 &&
        !ms_call_arg_is_scalar(call, (u32)lib->write_size_arg2))
        return false;
    return true;
}

const MsAllocFamily *ms_alloc_family_for_call(const char *name,
                                              const IrInst *call)
{
    const MsAllocFamily *family = ms_alloc_family_lookup(name);

    if (!family || !call || call->op != IR_CALL ||
        !alloc_family_fits_call(family, call))
        return NULL;
    return family;
}

const MsLibSummary *ms_lib_summary_for_call(const char *name,
                                            const IrInst *call)
{
    const MsLibSummary *lib = ms_lib_summary_lookup(name);

    if (!lib || !call || call->op != IR_CALL ||
        !lib_summary_fits_call(lib, call))
        return NULL;
    return lib;
}

bool ms_alloc_seed_for_call(const IrModule *module, const IrInst *call,
                            AliasAllocSeed *out)
{
    const MsAllocFamily *family;

    if (!module || !call || !out || call->op != IR_CALL ||
        call->subop != FUNCREF_EXTERNAL || call->callee >= module->nsyms)
        return false;
    family = ms_alloc_family_for_call(module->syms[call->callee], call);
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
