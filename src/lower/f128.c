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
#include "util/arena.h"

/* The runtime's names, which are libgcc's.
 *
 * __negtf2 is here rather than inline because the inline form is a NEON
 * sign-bit `eor vD.16b`, and the bundled assembler cannot yet encode NEON
 * register operands. It is a real libgcc entry point either way, so the call
 * links against libgcc too. `0 - x` is NOT a substitute for either form --
 * it turns -0.0 into +0.0. */
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
    case IR_FNEG:
        return "__negtf2";
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

/* --- comparisons -----------------------------------------------------------
 *
 * These cannot use the in-place trick: a comparison becomes a CALL whose
 * integer result is then tested, which is two instructions where the
 * arithmetic rewrite was one. The shape is
 *
 *     %t = call i32 @__lttf2(f128 %a, f128 %b)   <- inserted
 *     %r = icmp slt i32 %t, 0                    <- the fcmp, rewritten
 *
 * so the ORIGINAL value id keeps holding the boolean and every use of it is
 * untouched.
 *
 * The sense of each test is libgcc's documented contract, which is stated in
 * terms of "neither argument is NaN": __lttf2 returns a value less than zero
 * iff both are ordered and a < b, and so on down the family. That makes the
 * unordered-true predicates fall out as the NEGATED ordered test against the
 * same call -- ULT is `!(a >= b)`, which is `__getf2(a,b) < 0`. */
typedef struct CmpPlan {
    const char *call;  /* NULL: needs the two-call form below */
    u8 pred;           /* IrIcmp applied to the call result vs 0 */
    const char *call2; /* ONE/UEQ only */
    u8 pred2;
    u8 combine; /* ONE/UEQ only: IR_AND or IR_OR over the two tests */
} CmpPlan;

static bool compare_plan(u8 pred, CmpPlan *out)
{
    memset(out, 0, sizeof(*out));
    switch (pred) {
    case FCMP_OEQ:
        out->call = "__eqtf2";
        out->pred = ICMP_EQ;
        return true;
    case FCMP_UNE:
        out->call = "__netf2";
        out->pred = ICMP_NE;
        return true;
    case FCMP_OLT:
        out->call = "__lttf2";
        out->pred = ICMP_SLT;
        return true;
    case FCMP_OLE:
        out->call = "__letf2";
        out->pred = ICMP_SLE;
        return true;
    case FCMP_OGT:
        out->call = "__gttf2";
        out->pred = ICMP_SGT;
        return true;
    case FCMP_OGE:
        out->call = "__getf2";
        out->pred = ICMP_SGE;
        return true;
    /* The unordered-true relations are the ordered ones negated, which is
     * the SAME call with the complementary integer test -- no extra work. */
    case FCMP_UGE:
        out->call = "__lttf2";
        out->pred = ICMP_SGE;
        return true;
    case FCMP_UGT:
        out->call = "__letf2";
        out->pred = ICMP_SGT;
        return true;
    case FCMP_ULE:
        out->call = "__gttf2";
        out->pred = ICMP_SLE;
        return true;
    case FCMP_ULT:
        out->call = "__getf2";
        out->pred = ICMP_SLT;
        return true;
    case FCMP_UNO:
        out->call = "__unordtf2";
        out->pred = ICMP_NE;
        return true;
    case FCMP_ORD:
        out->call = "__unordtf2";
        out->pred = ICMP_EQ;
        return true;
    /* ONE and UEQ are the only two the runtime cannot answer in one call:
     * __netf2 reports "unequal OR unordered", so isolating the ordered half
     * needs __unordtf2 as well. C's operators never produce these, but the
     * optimizer can by negating a condition, and an ICE here would be a
     * crash on valid input. */
    case FCMP_ONE:
        out->call = "__unordtf2";
        out->pred = ICMP_EQ;
        out->call2 = "__netf2";
        out->pred2 = ICMP_NE;
        out->combine = IR_AND;
        return true;
    case FCMP_UEQ:
        out->call = "__unordtf2";
        out->pred = ICMP_NE;
        out->call2 = "__eqtf2";
        out->pred2 = ICMP_EQ;
        out->combine = IR_OR;
        return true;
    default:
        return false;
    }
}

static ValueId alloc_value(IrModule *m, IrFunc *f, BlockId block)
{
    ValueId value;

    if (f->nvals == f->cap_vals) {
        u32 capacity = f->cap_vals ? f->cap_vals * 2 : 16;
        IrValInfo *values = arena_alloc(m->arena, capacity * sizeof(*values),
                                        _Alignof(IrValInfo));

        if (f->nvals)
            memcpy(values, f->vals, f->nvals * sizeof(*values));
        f->vals = values;
        f->cap_vals = capacity;
    }
    memset(&f->vals[f->nvals], 0, sizeof(f->vals[f->nvals]));
    f->vals[f->nvals].type = (u8)IRT_I32;
    f->vals[f->nvals].def_kind = VDEF_INST;
    f->vals[f->nvals].def_block = block;
    value.v = ++f->nvals;
    return value;
}

/* Splice one instruction in ahead of `before`, keeping *prev the cursor the
 * caller's walk uses. */
static IrInst *splice_before(IrModule *m, IrBlock *block, IrInst **prev,
                             IrInst *before)
{
    IrInst *in = arena_alloc(m->arena, sizeof(*in), _Alignof(IrInst));

    memset(in, 0, sizeof(*in));
    in->loc = before->loc;
    in->next = before;
    if (*prev)
        (*prev)->next = in;
    else
        block->first = in;
    *prev = in;
    block->ninsts++;
    return in;
}

/* `%v = call i32 @name(f128 a, f128 b)`, inserted before `at`. */
static ValueId insert_libcall(IrModule *m, IrFunc *f, IrBlock *block,
                              BlockId block_id, IrInst **prev, IrInst *at,
                              const char *name)
{
    IrInst *call = splice_before(m, block, prev, at);

    call->op = IR_CALL;
    call->type = IRT_I32;
    call->subop = FUNCREF_EXTERNAL;
    call->callee = ir_sym(m, name);
    call->result = alloc_value(m, f, block_id);
    call->ops =
        arena_alloc(m->arena, 2 * sizeof(*call->ops), _Alignof(IrOperand));
    call->ops[0] = at->ops[0];
    call->ops[1] = at->ops[1];
    call->nops = 2;
    return call->result;
}

static IrOperand value_operand(ValueId v, IrType type)
{
    IrOperand op;

    memset(&op, 0, sizeof(op));
    op.kind = IROP_VALUE;
    op.type = (u8)type;
    op.a = v.v;
    return op;
}

static IrOperand zero_operand(void)
{
    IrOperand op;

    memset(&op, 0, sizeof(op));
    op.kind = IROP_ICONST;
    op.type = IRT_I32;
    return op;
}

/* `%v = icmp <pred> i32 %x, 0`, inserted before `at`. */
static ValueId insert_test(IrModule *m, IrFunc *f, IrBlock *block,
                           BlockId block_id, IrInst **prev, IrInst *at,
                           ValueId call_result, u8 pred)
{
    IrInst *test = splice_before(m, block, prev, at);

    test->op = IR_ICMP;
    test->type = IRT_I32;
    test->subop = pred;
    test->result = alloc_value(m, f, block_id);
    test->ops =
        arena_alloc(m->arena, 2 * sizeof(*test->ops), _Alignof(IrOperand));
    test->ops[0] = value_operand(call_result, IRT_I32);
    test->ops[1] = zero_operand();
    test->nops = 2;
    return test->result;
}

static void rewrite_compare(IrModule *m, IrFunc *f, IrBlock *block,
                            BlockId block_id, IrInst **prev, IrInst *in,
                            const CmpPlan *plan)
{
    ValueId first = insert_libcall(m, f, block, block_id, prev, in, plan->call);

    if (!plan->call2) {
        /* One call: the fcmp itself becomes the test of its result. */
        in->op = IR_ICMP;
        in->type = IRT_I32;
        in->subop = plan->pred;
        in->flags &= (u8)~IRF_NSW;
        in->ops[0] = value_operand(first, IRT_I32);
        in->ops[1] = zero_operand();
        in->nops = 2;
        return;
    }
    {
        ValueId second =
            insert_libcall(m, f, block, block_id, prev, in, plan->call2);
        ValueId a =
            insert_test(m, f, block, block_id, prev, in, first, plan->pred);
        ValueId b =
            insert_test(m, f, block, block_id, prev, in, second, plan->pred2);

        in->op = plan->combine;
        in->type = IRT_I32;
        in->subop = 0;
        in->flags &= (u8)~IRF_NSW;
        in->ops[0] = value_operand(a, IRT_I32);
        in->ops[1] = value_operand(b, IRT_I32);
        in->nops = 2;
    }
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
            IrBlock *block = &f->blocks[bi];
            IrInst *in, *prev = NULL;

            for (in = block->first; in; prev = in, in = in->next) {
                const char *name = NULL;
                CmpPlan plan;

                if (in->op == IR_FCMP && in->nops == 2 &&
                    in->ops[0].type == IRT_F128) {
                    if (!compare_plan(in->subop, &plan))
                        CGF_ICE("f128: no libcall for FP predicate %u",
                                in->subop);
                    rewrite_compare(m, f, block, (BlockId){bi + 1}, &prev, in,
                                    &plan);
                    continue;
                }
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
