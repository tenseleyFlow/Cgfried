#include "cg/x86_64/peep.h"

#include <stdlib.h>
#include <string.h>

#include "util/base.h"

/* Sprint 53's post-RA window pass.  Windows never cross a block boundary.
 * Deletions rebuild the block and remap flags_src, because an instruction
 * index is part of the flags dependency and is not disposable metadata. */

static bool reg_eq(X64VReg a, X64VReg b)
{
    return a.v && a.v == b.v;
}

static bool operand_uses(const X64Operand *o, u32 reg)
{
    if (o->kind == X64O_VREG)
        return o->r.v == reg;
    if (o->kind == X64O_MEM)
        return o->mem.base.v == reg || o->mem.index.v == reg;
    return false;
}

static bool inst_uses(const X64Inst *in, u32 reg)
{
    u32 i;

    if (operand_uses(&in->a, reg) || operand_uses(&in->b, reg))
        return true;
    for (i = 0; i < in->nxuses; i++)
        if (in->xuses[i].r.v == reg)
            return true;
    return false;
}

static bool inst_defs(const X64Inst *in, u32 reg)
{
    return in->def.v == reg;
}

static bool inst_mentions_rsp(const X64Inst *in)
{
    return inst_uses(in, X64_RSP + 1) || inst_defs(in, X64_RSP + 1);
}

static bool implicit_register_effects(const X64Inst *in)
{
    switch (in->op) {
    case X64_OP_CALL:
    case X64_OP_ASM:
    case X64_OP_CQO:
    case X64_OP_IDIV:
    case X64_OP_DIV:
    case X64_OP_VASTART:
    case X64_OP_STACKSAVE:
    case X64_OP_STACKRESTORE:
    case X64_OP_ALLOCA_DYN:
    case X64_OP_RET:
    case X64_OP_JMP:
    case X64_OP_JCC:
    case X64_OP_JMPTBL:
    case X64_OP_UD2:
        return true;
    default:
        return false;
    }
}

static bool flags_def_dead(const X64Block *b, u32 producer)
{
    u32 i;

    for (i = producer + 1; i < b->n; i++)
        if ((b->insts[i].flags & X64IF_USES_FLAGS) &&
            b->insts[i].flags_src == producer)
            return false;
    return true;
}

static void remove_marked(X64Func *f, X64Block *b, const bool *remove)
{
    u32 old_n = b->n, i, n = 0;
    u32 *map = arena_alloc(f->arena, old_n * sizeof(*map), _Alignof(u32));
    X64Inst *out =
        arena_alloc(f->arena, old_n * sizeof(*out), _Alignof(X64Inst));

    for (i = 0; i < old_n; i++) {
        map[i] = UINT32_MAX;
        if (!remove[i]) {
            map[i] = n;
            out[n++] = b->insts[i];
        }
    }
    for (i = 0; i < n; i++) {
        X64Inst *in = &out[i];

        if (!(in->flags & X64IF_USES_FLAGS))
            continue;
        if (in->flags_src >= old_n || map[in->flags_src] == UINT32_MAX)
            CGF_ICE("x64 peephole removed a live flags producer");
        in->flags_src = map[in->flags_src];
    }
    b->insts = out;
    b->n = n;
    b->cap = old_n;
}

static bool is_full_l_gp_def(const X64Inst *in, u32 reg)
{
    if (in->width != X64_L || !reg || reg > X64_GP_COUNT || in->def.v != reg)
        return false;
    switch (in->op) {
    case X64_OP_MOV:
    case X64_OP_MOVZX:
    case X64_OP_MOVSX:
    case X64_OP_LEA:
    case X64_OP_ADD:
    case X64_OP_SUB:
    case X64_OP_AND:
    case X64_OP_OR:
    case X64_OP_XOR:
    case X64_OP_IMUL:
    case X64_OP_NEG:
    case X64_OP_NOT:
    case X64_OP_SHL:
    case X64_OP_SHR:
    case X64_OP_SAR:
    case X64_OP_LOAD:
    case X64_OP_CVTF2I:
    case X64_OP_MOVQRX:
        return true;
    default:
        return false;
    }
}

static bool self_movs(X64Func *f, X64Block *b)
{
    bool *remove, changed = false;
    u32 i;

    if (!b->n)
        return false;
    remove = arena_alloc(f->arena, b->n * sizeof(*remove), _Alignof(bool));
    memset(remove, 0, b->n * sizeof(*remove));
    for (i = 0; i < b->n; i++) {
        X64Inst *in = &b->insts[i];

        /* movl %r,%r is NOT a no-op: it clears the architectural upper
         * half.  Only the full-width GP copy is removable without value
         * provenance. */
        if (in->op == X64_OP_MOV && in->width == X64_Q && in->def.v &&
            in->a.kind == X64O_VREG && reg_eq(in->def, in->a.r)) {
            remove[i] = true;
            changed = true;
        } else if (i && in->op == X64_OP_MOV && in->width == X64_L &&
                   in->def.v && in->a.kind == X64O_VREG &&
                   reg_eq(in->def, in->a.r) &&
                   is_full_l_gp_def(&b->insts[i - 1], in->def.v)) {
            /* IR_ZEXT i32->i64 is represented by this self movl.  It is
             * redundant only when the immediately preceding instruction
             * already performed a real 32-bit architectural write to the
             * same physical GP register.  Calls/asm/READREG are deliberately
             * absent from is_full_l_gp_def: their ABI metadata is not proof
             * that the upper half was cleared. */
            remove[i] = true;
            changed = true;
        }
    }
    if (changed)
        remove_marked(f, b, remove);
    return changed;
}

static bool cmp_zero(X64Block *b)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < b->n; i++) {
        X64Inst *in = &b->insts[i];

        if (in->op != X64_OP_CMP || in->a.kind != X64O_VREG ||
            in->b.kind != X64O_IMM || in->b.imm != 0)
            continue;
        in->op = X64_OP_TEST;
        in->b = in->a;
        changed = true;
    }
    return changed;
}

static bool lea_multipliers(X64Func *f, X64Block *b)
{
    bool *remove, changed = false;
    u32 i;

    if (b->n < 3)
        return false;
    remove = arena_alloc(f->arena, b->n * sizeof(*remove), _Alignof(bool));
    memset(remove, 0, b->n * sizeof(*remove));
    for (i = 0; i + 2 < b->n; i++) {
        X64Inst *mv = &b->insts[i];
        X64Inst *sh = &b->insts[i + 1];
        X64Inst *add = &b->insts[i + 2];
        u8 scale;

        if (mv->op != X64_OP_MOV || mv->a.kind != X64O_VREG || !mv->def.v ||
            reg_eq(mv->def, mv->a.r) || sh->op != X64_OP_SHL ||
            sh->def.v != mv->def.v || sh->a.kind != X64O_VREG ||
            sh->a.r.v != mv->def.v || sh->b.kind != X64O_IMM || sh->b.imm < 1 ||
            sh->b.imm > 3 || add->op != X64_OP_ADD || add->def.v != mv->def.v ||
            add->a.kind != X64O_VREG || add->a.r.v != mv->def.v ||
            add->b.kind != X64O_VREG || add->b.r.v != mv->a.r.v ||
            mv->width != sh->width || mv->width != add->width ||
            (mv->width != X64_L && mv->width != X64_Q) ||
            !flags_def_dead(b, i + 1) || !flags_def_dead(b, i + 2))
            continue;
        scale = (u8)(1u << sh->b.imm);
        memset(&mv->a, 0, sizeof(mv->a));
        mv->op = X64_OP_LEA;
        mv->flags = 0;
        mv->a.kind = X64O_MEM;
        mv->a.mem.base = add->b.r;
        mv->a.mem.index = add->b.r;
        mv->a.mem.scale = scale;
        remove[i + 1] = true;
        remove[i + 2] = true;
        changed = true;
        i += 2;
    }
    if (changed)
        remove_marked(f, b, remove);
    return changed;
}

static bool push_pop_pairs(X64Func *f, X64Block *b)
{
    bool *remove, changed = false;
    u32 i;

    if (b->n < 2)
        return false;
    remove = arena_alloc(f->arena, b->n * sizeof(*remove), _Alignof(bool));
    memset(remove, 0, b->n * sizeof(*remove));
    for (i = 0; i + 1 < b->n; i++) {
        X64Inst *push = &b->insts[i];
        u32 j;

        if (remove[i] || push->op != X64_OP_PUSH || push->a.kind != X64O_VREG ||
            !push->a.r.v || push->a.r.v == X64_RSP + 1)
            continue;
        for (j = i + 1; j < b->n && j <= i + 8; j++) {
            X64Inst *mid = &b->insts[j];

            if (mid->op == X64_OP_PUSH)
                break;
            if (mid->op == X64_OP_POP) {
                u32 dst = mid->def.v, k;
                bool legal = dst && dst != X64_RSP + 1;

                for (k = i + 1; legal && k < j; k++) {
                    X64Inst *x = &b->insts[k];

                    if (inst_mentions_rsp(x) || implicit_register_effects(x) ||
                        x->op == X64_OP_PUSH || x->op == X64_OP_POP ||
                        inst_uses(x, dst) || inst_defs(x, dst))
                        legal = false;
                    if (dst == push->a.r.v && inst_defs(x, dst))
                        legal = false;
                }
                if (legal) {
                    X64Inst mov;

                    memset(&mov, 0, sizeof(mov));
                    mov.op = X64_OP_MOV;
                    mov.width = X64_Q;
                    mov.def = mid->def;
                    mov.a = push->a;
                    mov.loc = push->loc;
                    *push = mov;
                    remove[j] = true;
                    changed = true;
                }
                break;
            }
            if (inst_mentions_rsp(mid) || implicit_register_effects(mid))
                break;
        }
    }
    if (changed)
        remove_marked(f, b, remove);
    return changed;
}

static bool setcc_refusion(X64Func *f)
{
    X64Func live_func = *f;
    u64 *live_in, *live_out;
    u32 words;
    bool any = false;
    u32 bi;

    if (!f->nblocks)
        return false;
    /* The shared backend dataflow is the source of truth for MIR CFGs: it
     * sees mid-block switch JCCs and every jump-table target.  A terminator-
     * only successor walk silently misses both.  Post-RA ids are physical
     * register numbers, so widen a small function's old virtual-id bound to
     * cover every possible allocated register before invoking the view. */
    if (live_func.nvregs < X64_REG_COUNT)
        live_func.nvregs = X64_REG_COUNT;
    words = x64_liveness_words(&live_func);
    live_in = cgf_xmalloc((size_t)f->nblocks * words * sizeof(*live_in));
    live_out = cgf_xmalloc((size_t)f->nblocks * words * sizeof(*live_out));
    memset(live_in, 0, (size_t)f->nblocks * words * sizeof(*live_in));
    memset(live_out, 0, (size_t)f->nblocks * words * sizeof(*live_out));
    x64_liveness(&live_func, live_in, live_out);
    for (bi = 0; bi < f->nblocks; bi++) {
        X64Block *b = &f->blocks[bi];
        bool *remove;
        bool block_changed = false;
        u32 i;

        if (b->n < 3)
            continue;
        remove = arena_alloc(f->arena, b->n * sizeof(*remove), _Alignof(bool));
        memset(remove, 0, b->n * sizeof(*remove));
        for (i = 0; i + 2 < b->n; i++) {
            X64Inst *set = &b->insts[i];
            u32 ti = i + 1, ji, reg;
            X64Inst *zext = NULL, *test, *jcc;

            if (set->op != X64_OP_SETCC || !(set->flags & X64IF_USES_FLAGS) ||
                set->width != X64_B || !set->def.v || set->def.v > X64_GP_COUNT)
                continue;
            reg = set->def.v;
            if (ti < b->n && b->insts[ti].op == X64_OP_MOVZX &&
                b->insts[ti].src_width == X64_B &&
                b->insts[ti].a.kind == X64O_VREG && b->insts[ti].a.r.v == reg &&
                b->insts[ti].def.v == reg) {
                zext = &b->insts[ti];
                ti++;
            }
            ji = ti + 1;
            if (ji >= b->n)
                continue;
            test = &b->insts[ti];
            jcc = &b->insts[ji];
            if (test->op != X64_OP_TEST || test->a.kind != X64O_VREG ||
                test->b.kind != X64O_VREG || test->a.r.v != reg ||
                test->b.r.v != reg || jcc->op != X64_OP_JCC ||
                jcc->cc != X64_CC_NE || !(jcc->flags & X64IF_USES_FLAGS) ||
                !(test->flags & X64IF_DEFS_FLAGS) || jcc->flags_src != ti ||
                ji + 1 != b->n ||
                (zext ? (zext->width != test->width ||
                         (zext->width != X64_W && zext->width != X64_L &&
                          zext->width != X64_Q))
                      : test->width != X64_B) ||
                ((live_out[(size_t)bi * words + (reg >> 6)] >> (reg & 63)) &
                 1u))
                continue;
            jcc->cc = set->cc;
            jcc->flags_src = set->flags_src;
            remove[i] = true;
            if (ti != i + 1)
                remove[i + 1] = true;
            remove[ti] = true;
            block_changed = true;
            any = true;
            i = ji;
        }
        if (block_changed)
            remove_marked(f, b, remove);
    }
    free(live_in);
    free(live_out);
    return any;
}

static bool jmp_to_next(X64Func *f)
{
    bool changed = false;
    u32 bi;

    for (bi = 0; bi + 1 < f->nblocks; bi++) {
        X64Block *b = &f->blocks[bi];
        bool *remove;

        if (!b->n || b->insts[b->n - 1].op != X64_OP_JMP ||
            b->insts[b->n - 1].target != bi + 2)
            continue;
        remove = arena_alloc(f->arena, b->n * sizeof(*remove), _Alignof(bool));
        memset(remove, 0, b->n * sizeof(*remove));
        remove[b->n - 1] = true;
        remove_marked(f, b, remove);
        changed = true;
    }
    return changed;
}

bool x64_peep_once(X64Func *f)
{
    bool changed = false;
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        changed |= cmp_zero(&f->blocks[bi]);
        changed |= lea_multipliers(f, &f->blocks[bi]);
        changed |= push_pop_pairs(f, &f->blocks[bi]);
        changed |= self_movs(f, &f->blocks[bi]);
    }
    changed |= setcc_refusion(f);
    changed |= jmp_to_next(f);
    return changed;
}

void x64_peep(X64Func *f)
{
    u32 iter;

    for (iter = 0; iter < 10; iter++)
        if (!x64_peep_once(f))
            return;
    if (x64_peep_once(f))
        CGF_ICE("x64 peephole did not converge after 10 iterations");
}
