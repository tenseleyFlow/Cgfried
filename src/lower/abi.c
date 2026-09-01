#include "lower/lower.h"

#include <string.h>

/* SysV x86-64 ABI plans (Sprint 19). This file is the ONE place the psABI
 * argument/return contract lives — lowering consults it at every function
 * definition and call site, and backends stay dumb (clang's strategy).
 *
 * The classifier itself is Sprint 14's layout_classify_sysv; this file
 * only MAPS classifications to IR shapes and never re-derives a rule —
 * packed structs classifying MEMORY even when small, mixed occupied classes
 * poisoning X87 aggregates, all of that is the classifier's word and we
 * trust it. A sole X87/X87UP aggregate deliberately survives for the
 * argument/return direction split below.
 *
 * Sprint 18's abstract call shapes become:
 *   INTEGER/SSE eightbytes -> 1-2 bit-carrying i64/f64 scalar args in
 *     eightbyte order (register assignment falls out of walking the IR
 *     arg types in order: i8..i64/ptr consume rdi,rsi,rdx,rcx,r8,r9 and
 *     f32/f64 consume xmm0..7 — no metadata needed);
 *   MEMORY -> the Sprint 18 ptr-to-caller-copy, now annotated byval(N):
 *     codegen copies the POINTEE onto the stack (16-byte slots for
 *     16-aligned types); the pointer itself never travels at runtime;
 *   returns per AbiRet — see ir.h's IrAbiRet contract for the pair/sret
 *     register story Sprint 23 implements.
 *
 * `long double` never invents argument-register passing: X87/X87UP arguments
 * go by value in memory. Returns are different: both bare f80 and an exact
 * 16-byte aggregate containing only one f80 use st0. IR-C-01 keeps that
 * direction split here instead of destroying the X87 classes in layout. */

static IrType eightbyte_irtype(AbiClass c)
{
    /* INTEGER carries pointers and ints; SSE carries float bits. Any
     * other class reaching an eightbyte plan is a classifier/plan drift
     * and dies loudly here rather than miscompiling a call. */
    switch (c) {
    case ABI_INTEGER:
        return IRT_I64;
    case ABI_SSE:
        return IRT_F64;
    default:
        CGF_ICE("abi: unexpected eightbyte class %d", (int)c);
    }
}

static bool sysv_whole_f128(int n, const AbiClass cls[2])
{
    return n == 2 && cls[0] == ABI_SSE && cls[1] == ABI_SSEUP;
}

static bool sysv_whole_f80(int n, const AbiClass cls[2])
{
    return n == 2 && cls[0] == ABI_X87 && cls[1] == ABI_X87UP;
}

/* --- AAPCS64 (Sprint 48) ---------------------------------------------------
 *
 * The parallel table against SysV, where the CONTRAST is the lesson:
 *
 *   case            SysV x86-64                  AAPCS64
 *   int args        rdi,rsi,rdx,rcx,r8,r9 (6)    x0-x7 (8)
 *   FP args         xmm0-7                       v0-v7 (s/d/q views)
 *   sret pointer    rdi — SHIFTS the real args   x8 — x0-x7 UNSHIFTED
 *   composite <=16  per-eightbyte, may split     x-reg pair, or v-regs if HFA
 *   composite >16   copied ONTO THE STACK        caller copy, POINTER in 1 GPR
 *   return >16      hidden rdi ptr, echoed rax   via x8, not preserved
 *
 * The composite>16 row is the #1 cross-ABI porting bug: on AAPCS64 a big
 * struct argument consumes one GPR, not a pile of stack bytes. The x8 row
 * is the second: x8 is not "argument 9", and a function returning a large
 * struct still receives its first real argument in x0.
 *
 * Register ASSIGNMENT (including the stage C NAF rule) belongs to the
 * backend walking these plans in order; this file only says what each
 * argument IS. */

static IrType hfa_leaf_irtype(Lower *lo, const Type *base)
{
    switch (base->kind) {
    case TY_FLOAT:
    case TY_FLOAT32:
        return IRT_F32;
    case TY_DOUBLE:
    case TY_FLOAT64:
    case TY_FLOAT32X:
        return IRT_F64;
    case TY_FLOAT128:
        return IRT_F128;
    case TY_LDOUBLE:
    case TY_FLOAT64X:
        /* These follow the target's long-double format: binary128/q on
         * arm64-linux, but plain double/d on arm64-macos. */
        return lower_irtype(lo, base);
    default:
        CGF_ICE("abi: HFA base type %d is not floating", (int)base->kind);
    }
}

static void classify_arg_aapcs64(Lower *lo, Type *t, AbiArg *out)
{
    TypeLayout l;
    Type *base = NULL;
    int leaves = 0;

    if (!lower_is_aggregate(t)) {
        out->kind = ABI_ARG_SCALAR;
        return;
    }
    l = layout_of(lo->sema, t);
    out->size = (u32)l.size;
    out->align = (u32)(l.align ? l.align : 1);

    if (layout_is_hfa(lo->sema, t, &base, &leaves)) {
        int i;

        if (leaves < 1 || leaves > ABI_MAX_HFA_LEAVES)
            CGF_ICE("abi: HFA has %d leaves, expected 1-%u", leaves,
                    (unsigned)ABI_MAX_HFA_LEAVES);
        out->kind = ABI_ARG_HFA;
        out->n = (u8)leaves;
        for (i = 0; i < leaves; i++)
            out->t[i] = hfa_leaf_irtype(lo, base);
        return;
    }
    /* Not an HFA: <=16 bytes travels in one or two general registers as
     * bit-carrying doublewords; anything larger becomes a caller-made
     * copy whose ADDRESS is the argument. */
    if (l.size <= 16) {
        out->kind = ABI_ARG_EIGHTBYTES;
        out->n = (u8)(l.size > 8 ? 2 : 1);
        out->t[0] = IRT_I64;
        if (out->n == 2)
            out->t[1] = IRT_I64;
        return;
    }
    out->kind = ABI_ARG_BYVAL;
}

static void classify_ret_aapcs64(Lower *lo, Type *t, AbiRet *out)
{
    TypeLayout l;
    Type *base = NULL;
    int leaves = 0;

    if (!t || t->kind == TY_VOID) {
        out->kind = ABI_RET_VOID;
        return;
    }
    if (!lower_is_aggregate(t)) {
        out->kind = ABI_RET_SCALAR;
        return;
    }
    l = layout_of(lo->sema, t);
    out->size = (u32)l.size;
    out->align = (u32)(l.align ? l.align : 1);

    if (layout_is_hfa(lo->sema, t, &base, &leaves)) {
        /* AAPCS64 returns 1-4 homogeneous FP leaves in v0-v3, passing NO
         * hidden pointer. The IR keeps the sret SHAPE exactly as the pairs
         * do -- a hidden ptr parameter the callee builds into -- and
         * IrFunc.abi_ret plus abi_ret_n carry the register truth. */
        out->kind = ABI_RET_HFA;
        out->small_t = hfa_leaf_irtype(lo, base);
        out->ir_abi = out->small_t == IRT_F32    ? IR_ABIRET_HFA_F32
                      : out->small_t == IRT_F128 ? IR_ABIRET_HFA_F128
                                                 : IR_ABIRET_HFA_F64;
        out->arg_annot = IR_ARG_HFA;
        out->n = (u32)leaves;
        return;
    }
    if (l.size <= 8) {
        out->kind = ABI_RET_SMALL;
        out->small_t = IRT_I64;
        return;
    }
    if (l.size <= 16) {
        /* x0:x1. Kept sret-SHAPED in IR exactly as SysV's pair is; the
         * register truth rides IrFunc.abi_ret. */
        out->kind = ABI_RET_PAIR;
        out->ir_abi = IR_ABIRET_PAIR_II;
        out->arg_annot = IR_ARG_PAIR_II;
        return;
    }
    out->kind = ABI_RET_SRET;
    out->ir_abi = IR_ABIRET_SRET;
    out->arg_annot = IR_ARG_SRET;
}

/* Which psABI governs this translation unit.
 *
 * Apple follows AAPCS64 for how an AGGREGATE is classified -- HFAs, the
 * 16-byte pair, the indirect-result pointer in x8 -- so both arm64 targets
 * share this classifier. The Apple divergences are elsewhere and are handled
 * where they actually live:
 *
 *   varargs        every anonymous argument goes on the STACK, so there is
 *                  no register save area and va_list is a plain char *
 *   extension      the CALLER sign/zero-extends anything narrower than 32
 *                  bits; AAPCS64 leaves those high bits unspecified
 *   stack packing  stack arguments take their natural size and alignment
 *                  rather than an 8-byte slot each
 *
 * Keeping them out of here is deliberate: a target check inside the
 * aggregate classifier would imply Apple classifies aggregates differently,
 * which it does not. */
static bool target_is_aapcs64(Lower *lo, Span span)
{
    (void)span;
    switch (lo->sema->target.kind) {
    case CGF_TARGET_ARM64_LINUX:
    case CGF_TARGET_ARM64_MACOS:
        return true;
    default:
        return false;
    }
}

bool abi_is_aapcs64(Lower *lo)
{
    return target_is_aapcs64(lo, (Span){0});
}

void abi_classify_arg(Lower *lo, Type *t, AbiArg *out)
{
    TypeLayout l;
    AbiClass cls[2];
    int n;

    memset(out, 0, sizeof(*out));
    if (target_is_aapcs64(lo, (Span){0})) {
        classify_arg_aapcs64(lo, t, out);
        return;
    }
    if (!lower_is_aggregate(t)) {
        out->kind = ABI_ARG_SCALAR;
        return;
    }
    l = layout_of(lo->sema, t);
    out->size = (u32)l.size;
    out->align = (u32)(l.align ? l.align : 1);
    n = layout_classify_sysv(lo->sema, t, cls);
    if (n < 0) {
        out->kind = ABI_ARG_BYVAL;
        return;
    }
    /* SysV X87/X87UP is a return-register classification only. Arguments,
     * bare or aggregate, are passed by value in memory. */
    if (sysv_whole_f80(n, cls)) {
        out->kind = ABI_ARG_BYVAL;
        return;
    }
    out->kind = ABI_ARG_EIGHTBYTES;
    if (sysv_whole_f128(n, cls)) {
        out->n = 1;
        out->t[0] = IRT_F128;
        return;
    }
    out->n = (u8)n;
    out->t[0] = eightbyte_irtype(cls[0]);
    if (n == 2)
        out->t[1] = eightbyte_irtype(cls[1]);
}

void abi_classify_ret(Lower *lo, Type *t, AbiRet *out)
{
    TypeLayout l;
    AbiClass cls[2];
    int n;

    memset(out, 0, sizeof(*out));
    if (target_is_aapcs64(lo, (Span){0})) {
        classify_ret_aapcs64(lo, t, out);
        return;
    }
    if (!t || t->kind == TY_VOID) {
        out->kind = ABI_RET_VOID;
        return;
    }
    if (!lower_is_aggregate(t)) {
        out->kind = ABI_RET_SCALAR;
        return;
    }
    l = layout_of(lo->sema, t);
    out->size = (u32)l.size;
    out->align = (u32)(l.align ? l.align : 1);
    n = layout_classify_sysv(lo->sema, t, cls);
    if (n < 0) {
        out->kind = ABI_RET_SRET;
        out->ir_abi = IR_ABIRET_SRET;
        out->arg_annot = IR_ARG_SRET;
        return;
    }
    if (sysv_whole_f80(n, cls)) {
        /* IR-C-01: the expression layer still needs aggregate staging, so
         * use SMALL's one-wire-value path rather than pretending the source
         * type is scalar. The wire value itself is the f80 returned in st0. */
        out->kind = ABI_RET_SMALL;
        out->small_t = IRT_F80;
        return;
    }
    if (sysv_whole_f128(n, cls)) {
        out->kind = ABI_RET_SMALL;
        out->small_t = IRT_F128;
        return;
    }
    if (n == 1) {
        out->kind = ABI_RET_SMALL;
        out->small_t = eightbyte_irtype(cls[0]);
        return;
    }
    out->kind = ABI_RET_PAIR;
    if (cls[0] == ABI_INTEGER)
        out->ir_abi =
            cls[1] == ABI_INTEGER ? IR_ABIRET_PAIR_II : IR_ABIRET_PAIR_IS;
    else
        out->ir_abi =
            cls[1] == ABI_INTEGER ? IR_ABIRET_PAIR_SI : IR_ABIRET_PAIR_SS;
    out->arg_annot = (u8)(IR_ARG_SRET + (out->ir_abi - IR_ABIRET_SRET));
}

void abi_budget_init(Lower *lo, AbiBudget *b, const AbiRet *ret)
{
    memset(b, 0, sizeof(*b));
    if (!ret)
        return;
    /* A MEMORY return is a real argument at runtime: SysV spends rdi on it,
     * AAPCS64 spends x8, which is outside the argument bank. A PAIR or an
     * HFA is sret-SHAPED in our IR but passes nothing at all. */
    if (ret->kind == ABI_RET_SRET && !target_is_aapcs64(lo, (Span){0}))
        b->gp = 1;
}

/* Re-plan an aggregate as ceil(size/8) eightbyte leaves: on the stack it is
 * just BYTES, no leaf keeps a register class, and the size rounds up to a
 * multiple of 8. Both facts then fall out of the existing N-leaf machinery --
 * a stacked HFA of three floats occupies 16 bytes, not the 12 its leaves
 * would suggest.
 *
 * The leaf TYPE still records which bank ran out, because the backend has to
 * pin the same one. Moving 8 bytes of float through an f64 is a pure bit copy
 * on arm64; FP load/store never canonicalizes. */
static bool abi_replan_as_eightbytes(AbiArg *a, bool fp_bank)
{
    u32 words = (a->size + 7u) / 8u;
    IrType leaf = fp_bank ? IRT_F64 : IRT_I64;
    u32 k;

    if (words > ABI_MAX_STACK_LEAVES)
        return false;
    a->n = (u8)(words ? words : 1);
    for (k = 0; k < a->n; k++)
        a->t[k] = leaf;
    a->kind = (u8)ABI_ARG_STACK;
    return true;
}

void abi_arg_place(Lower *lo, AbiArg *a, AbiBudget *b, bool anon)
{
    u32 need_gp = 0;
    u32 need_fp = 0;
    u32 i;
    bool aapcs = target_is_aapcs64(lo, (Span){0});

    /* ABI-004. On Apple an anonymous argument never takes a register, but its
     * SHAPE still follows the ordinary composite rule -- measured against
     * clang targeting arm64-apple-macos, because the ledger's summary of
     * Sprint 50 ("every anonymous argument by value") is true only of the
     * ones that would have travelled in registers:
     *
     *   struct{char[8]}       str  x0, [sp]              by value,  8 bytes
     *   struct{float x 3}     str  x8, [sp] + str w8,#8  by value, 12 bytes
     *   struct{char[16]}      stp  x0, x1, [sp]          by value, 16 bytes
     *   struct{double x 3}    str  d0 + stp d1,d2        by value, 24 bytes
     *   struct{char[17]}      add  x8, sp, #8; str x8    INDIRECT, a pointer
     *
     * So an HFA goes by value at any size and a non-HFA over 16 bytes goes
     * indirect exactly as a NAMED one does -- which is why ABI_ARG_BYVAL is
     * absent below. It is already a pointer, and the marshaller already puts
     * it in the varargs area without spending a register.
     *
     * What was wrong is only the leaf granularity: the marshaller gives each
     * anonymous leaf a full eightbyte, so a three-FLOAT HFA occupied 24 bytes
     * where clang uses 12 in a 16-byte slot. Merging the leaves into
     * ceil(size/8) eightbytes fixes that and coincides with the natural
     * layout when the leaves are already 8 bytes wide.
     *
     * The stack-leaf cap cannot bite here: the widest supported HFA is four
     * binary128 leaves -- exactly 64 bytes, exactly 8 eightbytes.
     *
     * Register budget deliberately untouched: an anonymous argument neither
     * takes nor exhausts a register, so it must not pin a bank the way an
     * over-large NAMED aggregate does below. */
    if (anon && lo->sema->target.kind == CGF_TARGET_ARM64_MACOS &&
        (a->kind == ABI_ARG_EIGHTBYTES || a->kind == ABI_ARG_HFA)) {
        if (a->align >= 16)
            a->stack_align16 = 1;
        if (!abi_replan_as_eightbytes(a, false))
            CGF_ICE("abi_arg_place: anonymous aggregate of %u bytes needs "
                    "more than the %u-leaf stack plan; no supported HFA "
                    "exceeds four binary128 leaves",
                    a->size, (unsigned)ABI_MAX_STACK_LEAVES);
        return;
    }

    switch (a->kind) {
    case ABI_ARG_HFA:
        need_fp = a->n;
        break;
    case ABI_ARG_EIGHTBYTES:
        for (i = 0; i < a->n; i++) {
            if (a->t[i] == IRT_F32 || a->t[i] == IRT_F64 || a->t[i] == IRT_F128)
                need_fp++;
            else
                need_gp++;
        }
        break;
    case ABI_ARG_BYVAL:
        /* SysV: memory, no register. AAPCS64: INDIRECT -- the caller-made
         * copy's address rides one general register. */
        if (aapcs)
            b->gp++;
        return;
    default:
        return; /* SCALAR: the walk charges it from the IR type. */
    }

    /* IR-C-09 / AAPCS64 C.10: Linux rounds NGRN up to an even register for
     * a 16-byte-aligned composite. Apple deliberately deleted this rule, so
     * x1:x2 remains correct there. Record the skipped register on the WHOLE
     * argument plan: after lowering splits it into leaves, neither backend
     * can rediscover which leaf began the aligned C argument. */
    if (lo->sema->target.kind == CGF_TARGET_ARM64_LINUX && need_gp &&
        a->kind == ABI_ARG_EIGHTBYTES && a->align >= 16 && (b->gp & 1u)) {
        b->gp++;
        a->even_gp = 1;
    }

    if (b->gp + need_gp <= 6 + (aapcs ? 2u : 0u) && b->fp + need_fp <= 8) {
        b->gp += need_gp;
        b->fp += need_fp;
        return;
    }

    /* It does not fit. SysV 3.2.3 step 5: "if there are no registers
     * available for ANY eightbyte of an argument, the whole argument is
     * passed on the stack" -- never half in r9 and half in memory, which is
     * what we did until the Sprint 51 ABI differential caught it on
     * `(int, long, char, struct{char[16]}, struct{char[15]})`.
     *
     * AAPCS64 says the same in C.4 and C.12 and then goes further: the
     * exhausted bank is PINNED at 8, so every later argument of that class
     * also goes to the stack even if a register happens to be free. That
     * makes it a property of the walk, not of the argument. */
    if (!aapcs) {
        /* SysV: the byval form already means by-value-on-the-stack, and the
         * exhausted bank is NOT pinned -- a later argument that does fit
         * still gets its register. */
        a->kind = (u8)ABI_ARG_STACK;
        return;
    }

    /* AAPCS64 pins the exhausted bank at 8, so every later argument of that
     * class is stacked too. */
    if (need_fp)
        b->fp = 8;
    if (need_gp)
        b->gp = 8;

    if (a->align >= 16)
        a->stack_align16 = 1;

    if (!abi_replan_as_eightbytes(a, need_fp != 0))
        CGF_ICE("abi_arg_place: stacked aggregate of %u bytes needs %u "
                "eightbytes, over the %u-leaf plan",
                a->size, (a->size + 7u) / 8u, (unsigned)ABI_MAX_STACK_LEAVES);
}
