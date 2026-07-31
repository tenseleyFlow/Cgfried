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

void abi_classify_arg(Lower *lo, Type *t, AbiArg *out)
{
    TypeLayout l;
    AbiClass cls[2];
    int n;

    memset(out, 0, sizeof(*out));
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

void abi_arg_regs(const AbiArg *a, u32 *gp, u32 *fp)
{
    u32 i;

    switch (a->kind) {
    case ABI_ARG_BYVAL:
        return; /* memory arguments consume no registers */
    case ABI_ARG_EIGHTBYTES:
        for (i = 0; i < a->n; i++) {
            if (a->t[i] == IRT_F64)
                (*fp)++;
            else
                (*gp)++;
        }
        return;
    default:
        return; /* SCALAR: the caller counts from the IR type */
    }
}
