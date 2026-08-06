#include "lower/lower.h"

#include <string.h>

/* SysV x86-64 ABI plans (Sprint 19). This file is the ONE place the psABI
 * argument/return contract lives — lowering consults it at every function
 * definition and call site, and backends stay dumb (clang's strategy).
 *
 * The classifier itself is Sprint 14's layout_classify_sysv; this file
 * only MAPS classifications to IR shapes and never re-derives a rule —
 * packed structs classifying MEMORY even when small, x87 members forcing
 * MEMORY, all of that is the classifier's word and we trust it.
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
 * `long double` never invents register passing: as an argument it is
 * MEMORY-class by slot on the stack (psABI §3.2.3 classifies X87/X87UP
 * eightbytes, and arguments of class X87 are passed in memory); as a
 * SCALAR return it stays IR type f80, returned on the x87 stack
 * (Sprint 23 emits the fld). An f80 inside an AGGREGATE classifies the
 * whole aggregate MEMORY (Sprint 14's job). */

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

static IrType hfa_leaf_irtype(const Type *base)
{
    switch (base->kind) {
    case TY_FLOAT:
        return IRT_F32;
    case TY_DOUBLE:
        return IRT_F64;
    case TY_LDOUBLE:
        /* AAPCS64 long double is IEEE binary128 and travels in a q-reg;
         * the f128 leaf type exists, but every operation on it needs the
         * Sprint 49 soft-float runtime. */
        return IRT_F128;
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

        out->kind = ABI_ARG_HFA;
        out->n = (u8)leaves;
        for (i = 0; i < leaves && i < ABI_MAX_LEAVES; i++)
            out->t[i] = hfa_leaf_irtype(base);
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
        /* AAPCS64 returns 1-4 homogeneous FP leaves in v0-v3, passing no
         * hidden pointer at all. That is a THIRD return shape, and it is
         * not yet implemented end to end -- see abi_ret_unsupported below
         * for why it cannot simply borrow the sret one. */
        out->kind = ABI_RET_HFA;
        out->small_t = hfa_leaf_irtype(base);
        out->ir_abi = IR_ABIRET_SRET; /* placeholder; see the note above */
        out->arg_annot = IR_ARG_SRET;
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
    out->kind = ABI_ARG_EIGHTBYTES;
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

/* AAPCS64's HFA return is classified but not yet lowered, and the reason it
 * cannot just reuse the sret shape is worth stating: sret passes a hidden
 * pointer in x8, while an HFA passes NOTHING and comes back in v0-v3. The
 * backend distinguishes return shapes through IrFunc.abi_ret, so the two
 * need distinct values there -- which means a new IrAbiRet, its round trip
 * through print/parse/struct_eq, a verifier rule, and both sides of a call
 * in the arm64 backend.
 *
 * Until that lands this is a clean error rather than an ICE. It fires on
 * `struct { float x, y, z; }` and friends -- common in graphics and math
 * code -- so it is not an exotic corner. See .docs/audits/abi-debt.md. */
bool abi_ret_unsupported(Lower *lo, const AbiRet *a, Span span)
{
    if (a->kind != ABI_RET_HFA)
        return false;
    lower_unimplemented(lo, span,
                        "returning a homogeneous floating-point aggregate "
                        "(AAPCS64 v0-v3 return)",
                        51);
    return true;
}

void abi_arg_regs(const AbiArg *a, u32 *gp, u32 *fp)
{
    u32 i;

    switch (a->kind) {
    case ABI_ARG_BYVAL:
        /* SysV: a memory argument consumes no register. AAPCS64 spends
         * one GPR on the pointer — the caller counts that where it walks
         * the IR, since the pointer is an ordinary ptr-typed argument
         * there rather than an annotation-only one. */
        return;
    case ABI_ARG_HFA:
        *fp += a->n;
        return;
    case ABI_ARG_EIGHTBYTES:
        for (i = 0; i < a->n; i++) {
            if (a->t[i] == IRT_F32 || a->t[i] == IRT_F64)
                (*fp)++;
            else
                (*gp)++;
        }
        return;
    default:
        return; /* SCALAR: the caller counts from the IR type */
    }
}
