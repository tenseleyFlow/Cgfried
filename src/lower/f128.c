/* Sprint 49: binary128 legalization — f128 operations become soft-float calls.
 *
 * arm64-linux's `long double` is IEEE binary128, and the architecture has no
 * instructions for it. Every operation is a call into libcgf_rt, whose 23
 * entry points landed in Sprint 49 D4 and are byte-identical to libgcc's over
 * 1400 cases. This pass is the other half of that: the codegen side, turning
 * the IR's native f128 opcodes into those calls.
 *
 * WHY A PASS AND NOT LOWERING: f128 operations are produced at a dozen
 * lowering sites, and rewriting each would spread the target condition across
 * all of them. One walk over the finished module keeps the whole policy in
 * one file that a reader can check against the runtime's symbol list.
 *
 * WHY AFTER THE OPTIMIZER: `simplify.c` folds f128 arithmetic through the
 * same softfp core (Sprint 31), so legalizing earlier would hide constant
 * operations behind opaque calls and lose that folding. Running last means
 * the optimizer sees the arithmetic it understands and codegen sees only
 * calls.
 *
 * x86_64 keeps its x87 f80 long double and never runs this.
 */

#include "lower/f128.h"

#include <string.h>

#include "diag.h"
#include "ir/ir.h"

/* The runtime's names, which are libgcc's. Only these exist: notably there
 * is NO __negtf2, so IR_FNEG is left for the backend to do as a sign-bit
 * flip. `0 - x` is not a substitute — it turns -0.0 into +0.0. */
static const char *binary_libcall(u8 op)
{
    switch (op) {
    case IR_FADD:
        return "__addtf3";
    case IR_FSUB:
        return "__subtf3";
    case IR_FMUL:
        return "__multf3";
    case IR_FDIV:
        return "__divtf3";
    default:
        return NULL;
    }
}

/* Conversions INTO or OUT OF f128. The direction is read from the
 * instruction's result type versus its operand type, because one opcode
 * (IR_FPEXT) covers f32->f128 and f64->f128 both. */
static const char *convert_libcall(const IrFunc *f, const IrInst *in)
{
    IrType from = (IrType)in->ops[0].type;
    IrType to = (IrType)in->type;

    (void)f;
    switch (in->op) {
    case IR_FPEXT:
        if (to != IRT_F128)
            return NULL;
        return from == IRT_F32 ? "__extendsftf2" : "__extenddftf2";
    case IR_FPTRUNC:
        if (from != IRT_F128)
            return NULL;
        return to == IRT_F32 ? "__trunctfsf2" : "__trunctfdf2";
    case IR_FPTOSI:
        if (from != IRT_F128)
            return NULL;
        return to == IRT_I64 ? "__fixtfdi" : "__fixtfsi";
    case IR_FPTOUI:
        if (from != IRT_F128)
            return NULL;
        return to == IRT_I64 ? "__fixunstfdi" : "__fixunstfsi";
    case IR_SITOFP:
        if (to != IRT_F128)
            return NULL;
        return from == IRT_I64 ? "__floatditf" : "__floatsitf";
    case IR_UITOFP:
        if (to != IRT_F128)
            return NULL;
        return from == IRT_I64 ? "__floatunditf" : "__floatunsitf";
    default:
        return NULL;
    }
}

/* Rewrite one instruction into a call to `name`, in place.
 *
 * This works because a call's operands ARE its arguments and its type IS its
 * result: an f128 add and a call to __addtf3 have the same shape, so nothing
 * has to be inserted, no value ids move, and no block has to grow. Only the
 * opcode, the reference kind and the callee change. */
static void rewrite_as_call(IrModule *m, IrInst *in, const char *name)
{
    in->op = IR_CALL;
    in->subop = FUNCREF_EXTERNAL;
    in->callee = ir_sym(m, name);
    in->flags &= (u16)~IRF_NSW;
}

bool lower_f128_needs_libcalls(TargetSpec t)
{
    /* x86_64's long double is x87 f80, which the backend selects natively. */
    return t.kind == CGF_TARGET_ARM64_LINUX || t.kind == CGF_TARGET_ARM64_MACOS;
}

void lower_legalize_f128(IrModule *m, TargetSpec t)
{
    u32 fi, bi;

    if (!lower_f128_needs_libcalls(t))
        return;

    for (fi = 0; fi < m->nfuncs; fi++) {
        IrFunc *f = &m->funcs[fi];

        for (bi = 0; bi < f->nblocks; bi++) {
            IrInst *in;

            for (in = f->blocks[bi].first; in; in = in->next) {
                const char *name = NULL;

                if (in->type == IRT_F128)
                    name = binary_libcall(in->op);
                if (!name && in->nops >= 1)
                    name = convert_libcall(f, in);
                if (name)
                    rewrite_as_call(m, in, name);
            }
        }
    }
}
