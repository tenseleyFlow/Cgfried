#include "cg/x86_64/mir.h"

#include <string.h>

/* Shared MIR construction helpers (Sprint 23): class-tracked vreg
 * minting, the rodata constant pool, and implicit-use lists. Both isel
 * and regalloc (repair copies) mint vregs, so this lives here. */

X64VReg x64_newv(X64Func *f, X64RegClass rc)
{
    X64VReg r = {++f->nvregs};

    if (f->nvregs + 1 > f->cap_vclass) {
        u32 nc = f->cap_vclass ? f->cap_vclass * 2 : 64;
        u8 *nv;

        while (nc < f->nvregs + 1)
            nc *= 2;
        nv = arena_alloc(f->arena, nc, 1);
        if (f->cap_vclass)
            memcpy(nv, f->vclass, f->cap_vclass);
        memset(nv + f->cap_vclass, 0, nc - f->cap_vclass);
        f->vclass = nv;
        f->cap_vclass = nc;
    }
    f->vclass[f->nvregs] = (u8)rc;
    return r;
}

u8 x64_vclass(const X64Func *f, u32 v)
{
    if (!v || v > f->nvregs || !f->vclass)
        return X64RC_GP;
    return f->vclass[v];
}

u32 x64_cpool_intern(X64Func *f, u64 lo, u64 hi, u8 size, u8 align)
{
    u32 i;

    for (i = 0; i < f->nconsts; i++) {
        const X64Const *c = &f->consts[i];

        if (c->lo == lo && c->hi == hi && c->size == size && c->align == align)
            return i + 1;
    }
    if (f->nconsts == f->cap_consts) {
        u32 nc = f->cap_consts ? f->cap_consts * 2 : 8;
        X64Const *nv =
            arena_alloc(f->arena, nc * sizeof(X64Const), _Alignof(X64Const));

        if (f->nconsts)
            memcpy(nv, f->consts, f->nconsts * sizeof(X64Const));
        f->consts = nv;
        f->cap_consts = nc;
    }
    f->consts[f->nconsts].lo = lo;
    f->consts[f->nconsts].hi = hi;
    f->consts[f->nconsts].size = size;
    f->consts[f->nconsts].align = align;
    return ++f->nconsts;
}

void x64_add_xuse(X64Func *f, X64Inst *in, X64VReg r, u8 fixed)
{
    X64XUse *nv = arena_alloc(f->arena, (in->nxuses + 1) * sizeof(X64XUse),
                              _Alignof(X64XUse));

    if (in->nxuses)
        memcpy(nv, in->xuses, in->nxuses * sizeof(X64XUse));
    nv[in->nxuses].r = r;
    nv[in->nxuses].fixed = fixed;
    in->xuses = nv;
    in->nxuses++;
}
