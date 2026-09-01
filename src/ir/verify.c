#include "ir/ir.h"

#include <stdarg.h>
#include <string.h>

/* The verifier: ten numbered checks, run at every pipeline boundary. Each
 * diagnostic message begins with "ir verify [N]" so tests can pin WHICH
 * check fired structurally. The verifier only REPORTS (DIAG_ERROR through
 * the DiagCtx) and returns false; the caller owns severity — exit 1 for
 * hand-written .cgfir, ICE (after a CGF_DUMP_BAD_IR dump) for IR the
 * compiler generated itself.
 *
 * The check list is the sprint's, verbatim:
 *   1  every value operand's def dominates the use
 *   2  every edge's args match the target's param arity and types
 *   3  exactly one terminator per block, and it is last
 *   4  per-op type discipline
 *   5  entry block has no params and no predecessors
 *   6  no orphan blocks — unreachable-from-entry is REJECTED
 *   7  instruction flags only on ops that can carry their semantics
 *   8  alignments nonzero powers of two; load/store never over-aligned;
 *      seq_cst scalar load/store naturally aligned
 *   9  FuncRef/symbol indices in range; internal call arity/types match
 *   10 reserved opcodes absent */

typedef struct V {
    DiagCtx *dc;
    char *why; /* first-failure summary sink, or NULL */
    size_t why_cap;
    const IrModule *m;
    const IrFunc *f;
    const IrBlock *blk;
    const char *blk_name;
    IrDomTree *dom;
    bool *reach; /* by block index */
    Arena *scratch;
    bool ok;
} V;

static void verr(V *v, int check, const char *fmt, ...)
{
    va_list ap;
    char msg[256];
    Span sp = {0};

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (v->f && v->blk_name)
        diag_emit(v->dc, DIAG_ERROR, sp,
                  "ir verify [%d] in @%s, block '%s': %s", check, v->f->name,
                  v->blk_name, msg);
    else if (v->f)
        diag_emit(v->dc, DIAG_ERROR, sp, "ir verify [%d] in @%s: %s", check,
                  v->f->name, msg);
    else
        diag_emit(v->dc, DIAG_ERROR, sp, "ir verify [%d]: %s", check, msg);
    if (v->ok && v->why)
        snprintf(v->why, v->why_cap, "check %d in @%s: %s", check,
                 v->f ? v->f->name : "<module>", msg);
    v->ok = false;
}

static bool is_terminator(u8 op)
{
    return op >= IR_RET && op <= IR_UNREACHABLE;
}

static bool type_is_int(u8 t)
{
    return t <= IRT_I64;
}

static bool type_is_float(u8 t)
{
    return t >= IRT_F32 && t <= IRT_F128;
}

static u32 int_width(u8 t)
{
    return 8u << t; /* IRT_I8=0 .. IRT_I64=3 */
}

static u32 natural_align(u8 t)
{
    u32 size = ir_type_size((IrType)t);

    return size ? size : 1;
}

static bool pow2_nonzero(u32 a)
{
    return a != 0 && (a & (a - 1)) == 0;
}

/* --- check 1: dominance of uses ------------------------------------------ */

static void check_use(V *v, const IrOperand *o, BlockId use_blk, u32 use_pos)
{
    const IrValInfo *vi;
    u32 id;

    if (o->kind != IROP_VALUE)
        return;
    id = (u32)o->a;
    if (id == 0 || id > v->f->nvals) {
        verr(v, 1, "operand names value id %u, which does not exist", id);
        return;
    }
    vi = &v->f->vals[id - 1];
    switch (vi->def_kind) {
    case VDEF_FPARAM:
        return; /* defined at the entry's head; dominates everything the
                   entry dominates, and orphans are check 6's problem */
    case VDEF_BPARAM:
        if (vi->def_block.v == use_blk.v)
            return; /* params are defs at the block head */
        break;
    case VDEF_INST:
        if (vi->def_block.v == use_blk.v) {
            if (vi->def_pos >= use_pos)
                verr(v, 1,
                     "value defined at instruction %u is used at "
                     "instruction %u before its definition",
                     vi->def_pos, use_pos);
            return;
        }
        break;
    default:
        verr(v, 1, "value id %u has no definition", id);
        return;
    }
    if (!ir_dominates(v->dom, vi->def_block, use_blk)) {
        const IrBlock *db = ir_block((IrFunc *)v->f, vi->def_block);

        verr(v, 1,
             "use of a value whose defining block '%s' does not "
             "dominate this block",
             db && db->name ? db->name : "?");
    }
}

/* --- per-op type discipline (check 4) ------------------------------------ */

static void check_binop_types(V *v, const IrInst *in, bool want_float)
{
    IrType t = (IrType)in->type;

    if (ir_type_is_vector(t)) {
        bool legal = false;

        if (want_float) {
            legal = ir_type_is_vector_float(t) &&
                    (in->op == IR_FADD || in->op == IR_FSUB ||
                     in->op == IR_FMUL || in->op == IR_FDIV);
        } else if (ir_type_is_vector_int(t)) {
            legal = in->op == IR_IADD || in->op == IR_ISUB ||
                    in->op == IR_AND || in->op == IR_OR || in->op == IR_XOR ||
                    (in->op == IR_IMUL && t == IRT_V8I16);
        }
        if (!legal) {
            verr(v, 4, "'%s' is not supported for vector type %s under SSE2",
                 ir_op_name((IrOp)in->op), ir_type_name(t));
            return;
        }
        if (in->ops[0].type != in->type || in->ops[1].type != in->type)
            verr(v, 4, "'%s' vector operands must match result type %s",
                 ir_op_name((IrOp)in->op), ir_type_name(t));
        return;
    }
    if (want_float ? !type_is_float(in->type) : !type_is_int(in->type)) {
        verr(v, 4, "'%s' requires %s result type, got %s",
             ir_op_name((IrOp)in->op), want_float ? "a float" : "an integer",
             ir_type_name((IrType)in->type));
        return;
    }
    if (in->ops[0].type != in->type || in->ops[1].type != in->type)
        verr(v, 4,
             "'%s' operand types (%s, %s) do not match the result "
             "type %s",
             ir_op_name((IrOp)in->op), ir_type_name((IrType)in->ops[0].type),
             ir_type_name((IrType)in->ops[1].type),
             ir_type_name((IrType)in->type));
}

static void check_inst_types(V *v, const IrInst *in)
{
    if ((in->op == IR_ALLOCA || in->op == IR_LOAD || in->op == IR_STORE ||
         in->op == IR_MEMCPY || in->op == IR_MEMSET) &&
        in->subop >= ETYPE_COUNT) {
        verr(v, 4, "'%s' has bad effective type %u", ir_op_name((IrOp)in->op),
             in->subop);
        return;
    }
    switch (in->op) {
    case IR_IADD:
    case IR_ISUB:
    case IR_IMUL:
    case IR_SDIV:
    case IR_UDIV:
    case IR_SREM:
    case IR_UREM:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
    case IR_SHL:
    case IR_LSHR:
    case IR_ASHR:
        check_binop_types(v, in, false);
        break;
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FDIV:
        check_binop_types(v, in, true);
        break;
    case IR_FNEG:
        if (!type_is_float(in->type) || in->ops[0].type != in->type)
            verr(v, 4,
                 "'fneg' wants one float operand matching its "
                 "result type");
        break;
    case IR_VSPLAT: {
        IrType vt = (IrType)in->type;

        if (!ir_type_is_vector(vt) || in->nops != 1 ||
            in->ops[0].type != ir_vector_elem_type(vt))
            verr(v, 4, "'vsplat' takes one scalar matching its vector lanes");
        break;
    }
    case IR_VEXTRACT: {
        IrType vt = (IrType)in->ops[0].type;

        if (in->nops != 1 || !ir_type_is_vector(vt) ||
            in->type != ir_vector_elem_type(vt) ||
            in->subop >= ir_vector_lanes(vt))
            verr(v, 4,
                 "'vextract' needs a vector, its scalar result type, and an "
                 "in-range lane");
        break;
    }
    case IR_VREDUCE_ADD:
    case IR_VREDUCE_MUL:
    case IR_VREDUCE_AND:
    case IR_VREDUCE_OR:
    case IR_VREDUCE_XOR: {
        IrType vt = (IrType)in->ops[0].type;
        bool int_ok = ir_type_is_vector_int(vt);
        bool fp_ok = ir_type_is_vector_float(vt);
        bool legal = false;

        if (in->op == IR_VREDUCE_ADD)
            legal = int_ok || fp_ok;
        else if (in->op == IR_VREDUCE_MUL)
            legal = fp_ok || vt == IRT_V8I16;
        else
            legal = int_ok;
        if (in->nops != 1 || !legal || in->type != ir_vector_elem_type(vt))
            verr(v, 4, "'%s' has an unsupported vector/result type pair",
                 ir_op_name((IrOp)in->op));
        break;
    }
    case IR_ICMP:
        if (in->type != IRT_I32)
            verr(v, 4, "'icmp' produces i32");
        else if (in->ops[0].type != in->ops[1].type ||
                 (!type_is_int(in->ops[0].type) && in->ops[0].type != IRT_PTR))
            verr(v, 4,
                 "'icmp' compares two integers or two pointers of "
                 "one type");
        else if (in->subop > ICMP_UGE)
            verr(v, 4, "bad icmp predicate %u", in->subop);
        break;
    case IR_FCMP:
        if (in->type != IRT_I32)
            verr(v, 4, "'fcmp' produces i32");
        else if (in->ops[0].type != in->ops[1].type ||
                 !type_is_float(in->ops[0].type))
            verr(v, 4, "'fcmp' compares two floats of one type");
        else if (in->subop > FCMP_UNO)
            verr(v, 4, "bad fcmp predicate %u", in->subop);
        break;
    case IR_SEXT:
    case IR_ZEXT:
        if (!type_is_int(in->ops[0].type) || !type_is_int(in->type) ||
            int_width(in->ops[0].type) >= int_width(in->type))
            verr(v, 4,
                 "'%s' widens an integer to a strictly wider "
                 "integer",
                 ir_op_name((IrOp)in->op));
        break;
    case IR_TRUNC:
        if (!type_is_int(in->ops[0].type) || !type_is_int(in->type) ||
            int_width(in->ops[0].type) <= int_width(in->type))
            verr(v, 4,
                 "'trunc' narrows an integer to a strictly "
                 "narrower integer");
        break;
    case IR_FPEXT:
        if (!type_is_float(in->ops[0].type) || !type_is_float(in->type) ||
            in->ops[0].type >= in->type)
            verr(v, 4, "'fpext' widens a float to a strictly wider float");
        break;
    case IR_FPTRUNC:
        if (!type_is_float(in->ops[0].type) || !type_is_float(in->type) ||
            in->ops[0].type <= in->type)
            verr(v, 4,
                 "'fptrunc' narrows a float to a strictly narrower "
                 "float");
        break;
    case IR_FPTOSI:
    case IR_FPTOUI:
        if (!type_is_float(in->ops[0].type) || !type_is_int(in->type))
            verr(v, 4, "'%s' converts a float to an integer",
                 ir_op_name((IrOp)in->op));
        break;
    case IR_SITOFP:
    case IR_UITOFP:
        if (!type_is_int(in->ops[0].type) || !type_is_float(in->type))
            verr(v, 4, "'%s' converts an integer to a float",
                 ir_op_name((IrOp)in->op));
        break;
    case IR_BITCAST: {
        bool i2f = in->ops[0].type == IRT_I32 && in->type == IRT_F32;
        bool i2d = in->ops[0].type == IRT_I64 && in->type == IRT_F64;
        bool f2i = in->ops[0].type == IRT_F32 && in->type == IRT_I32;
        bool d2i = in->ops[0].type == IRT_F64 && in->type == IRT_I64;
        bool i2p = in->ops[0].type == IRT_I64 && in->type == IRT_PTR;
        bool p2i = in->ops[0].type == IRT_PTR && in->type == IRT_I64;

        if (!i2f && !i2d && !f2i && !d2i && !i2p && !p2i)
            verr(v, 4,
                 "'bitcast' is only defined between same-width "
                 "types (i32<->f32, i64<->f64, i64<->ptr)");
        break;
    }
    case IR_ALLOCA:
        if (in->type != IRT_PTR || in->ops[0].type != IRT_I64)
            verr(v, 4, "'alloca' takes an i64 byte size and produces ptr");
        break;
    case IR_LOAD:
        if (in->type == IRT_VOID || in->ops[0].type != IRT_PTR)
            verr(v, 4,
                 "'load' reads a non-void type through a ptr "
                 "operand");
        break;
    case IR_STORE:
        if (in->ops[1].type != IRT_PTR)
            verr(v, 4, "'store' writes through a ptr operand");
        break;
    case IR_PTRADD:
        if (in->type != IRT_PTR || in->ops[0].type != IRT_PTR ||
            in->ops[1].type != IRT_I64)
            verr(v, 4, "'ptradd' is ptr + i64 byte offset -> ptr");
        break;
    case IR_MEMCPY:
        if (in->ops[0].type != IRT_PTR || in->ops[1].type != IRT_PTR ||
            in->ops[2].type != IRT_I64)
            verr(v, 4, "'memcpy' is (ptr dst, ptr src, i64 size)");
        else if (in->subop != ETYPE_CHAR)
            verr(v, 4, "'memcpy' must have char effective type");
        break;
    case IR_MEMSET:
        if (in->ops[0].type != IRT_PTR || in->ops[1].type != IRT_I32 ||
            in->ops[2].type != IRT_I64)
            verr(v, 4, "'memset' is (ptr dst, i32 byte, i64 size)");
        else if (in->subop != ETYPE_CHAR)
            verr(v, 4, "'memset' must have char effective type");
        break;
    case IR_SELECT:
        if (ir_type_is_vector((IrType)in->type))
            verr(v, 4,
                 "vector 'select' is not supported by the Sprint 36 backend");
        else if (in->ops[0].type != IRT_I32)
            verr(v, 4, "'select' tests an i32 condition");
        else if (in->ops[1].type != in->type || in->ops[2].type != in->type)
            verr(v, 4, "'select' arm types must match the result type");
        break;
    case IR_VA_START:
        if (in->nops != 1 || in->ops[0].type != IRT_PTR)
            verr(v, 4, "'va_start' takes one ptr operand");
        else if (!v->f->variadic)
            verr(v, 4, "'va_start' in a non-variadic function");
        break;
    case IR_STACKSAVE:
        if (in->type != IRT_PTR || in->nops != 0)
            verr(v, 4, "'stacksave' takes nothing and produces ptr");
        break;
    case IR_ATOMICRMW:
        /* Check 13: atomic discipline. Integer types only (sizes are
         * powers of two by construction); ptr + same-type value; the
         * seq_cst flag is mandatory in v0.1.0. */
        if (!type_is_int(in->type) || in->nops != 2 ||
            in->ops[0].type != IRT_PTR || in->ops[1].type != in->type)
            verr(v, 13, "'atomicrmw' is (ptr, T val) -> T with integer T");
        else if (in->subop > RMW_XCHG)
            verr(v, 13, "bad atomicrmw operation %u", in->subop);
        else if (!(in->flags & IRF_SEQ_CST))
            verr(v, 13, "'atomicrmw' must be seq_cst in v0.1.0");
        break;
    case IR_CMPXCHG:
        if (!type_is_int(in->type) || in->nops != 3 ||
            in->ops[0].type != IRT_PTR || in->ops[1].type != in->type ||
            in->ops[2].type != in->type)
            verr(v, 13,
                 "'cmpxchg' is (ptr, T expected, T desired) -> T with "
                 "integer T");
        else if (!(in->flags & IRF_SEQ_CST))
            verr(v, 13, "'cmpxchg' must be seq_cst in v0.1.0");
        break;
    case IR_STACKRESTORE: {
        /* Check 12: the token must be a VALUE produced by a stacksave
         * (dominance is check 1's job; the OPCODE of the def is ours). */
        const IrOperand *tok = &in->ops[0];

        if (in->nops != 1 || tok->type != IRT_PTR) {
            verr(v, 12, "'stackrestore' takes one ptr token");
            break;
        }
        if (tok->kind == IROP_VALUE) {
            u32 id = (u32)tok->a;

            if (id >= 1 && id <= v->f->nvals &&
                v->f->vals[id - 1].def_kind == VDEF_INST) {
                const IrValInfo *vi = &v->f->vals[id - 1];
                const IrBlock *db = ir_block((IrFunc *)v->f, vi->def_block);
                const IrInst *di = db ? db->first : NULL;
                u32 pos = 0;

                while (di && pos < vi->def_pos) {
                    di = di->next;
                    pos++;
                }
                if (di && di->result.v == id && di->op == IR_STACKSAVE)
                    break; /* good token */
            }
        }
        verr(v, 12, "'stackrestore' token is not a stacksave result");
        break;
    }
    case IR_RET:
        if (v->f->ret == IRT_VOID) {
            if (in->nops != 0)
                verr(v, 4, "'ret' with a value in a void function");
        } else if (in->nops != 1 || in->ops[0].type != v->f->ret) {
            verr(v, 4, "'ret' must return exactly one %s",
                 ir_type_name((IrType)v->f->ret));
        }
        break;
    case IR_CONDBR:
        if (in->ops[0].type != IRT_I32)
            verr(v, 4, "'condbr' tests an i32 condition");
        break;
    case IR_SWITCH:
        if (!type_is_int(in->ops[0].type))
            verr(v, 4, "'switch' scrutinizes an integer");
        break;
    default:
        break;
    }
}

/* --- checks 7-9 per instruction ------------------------------------------ */

static void check_inst_misc(V *v, const IrInst *in)
{
    bool is_mem = in->op == IR_LOAD || in->op == IR_STORE ||
                  in->op == IR_MEMCPY || in->op == IR_MEMSET ||
                  in->op == IR_ALLOCA;
    u32 i;

    /* 7: flags only where they mean something. */
    if ((in->flags & IRF_VOLATILE) && in->op != IR_CALL) {
        if (!is_mem || in->op == IR_ALLOCA)
            verr(v, 7, "'volatile' on '%s', which cannot carry it",
                 ir_op_name((IrOp)in->op));
    }
    if (in->flags & IRF_SEQ_CST) {
        if (in->op != IR_LOAD && in->op != IR_STORE && in->op != IR_ATOMICRMW &&
            in->op != IR_CMPXCHG)
            verr(v, 7,
                 "'seq_cst' on '%s'; only load/store and the atomic ops "
                 "carry an ordering",
                 ir_op_name((IrOp)in->op));
    }
    if (in->flags & IRF_CALL_VARIADIC) {
        if (in->op != IR_CALL)
            verr(v, 7, "'va' on '%s'; only calls carry the AL protocol",
                 ir_op_name((IrOp)in->op));
    }
    if ((in->flags & IRF_NORETURN) && in->op != IR_CALL)
        verr(v, 7, "'noreturn' on '%s'; only calls carry it",
             ir_op_name((IrOp)in->op));
    if ((in->flags & IRF_SELF_INIT) && in->op != IR_LOAD)
        verr(v, 7, "'self_init' on '%s'; only loads carry it",
             ir_op_name((IrOp)in->op));
    if ((in->flags & IRF_FLOW_PROVENANCE) && in->op != IR_RET &&
        in->op != IR_BR && in->op != IR_CONDBR && in->op != IR_SWITCH)
        verr(v, 7, "flow provenance on '%s'; only terminators carry it",
             ir_op_name((IrOp)in->op));
    if (in->flags & IRF_NSW) {
        if (in->op != IR_IADD && in->op != IR_ISUB && in->op != IR_IMUL)
            verr(v, 7,
                 "'nsw' on '%s'; only integer add/sub/mul carry signed "
                 "overflow provenance",
                 ir_op_name((IrOp)in->op));
    }
    if (ir_type_is_vector((IrType)in->type) && (in->flags & IRF_NSW))
        verr(v, 7, "'nsw' is not defined on vector arithmetic");
    if ((in->op == IR_LOAD && ir_type_is_vector((IrType)in->type)) ||
        (in->op == IR_STORE && ir_type_is_vector((IrType)in->ops[0].type))) {
        if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST))
            verr(v, 7, "vector load/store cannot be volatile or atomic");
    }
    if ((in->flags & IRF_BOUNDS_CHECK) && in->op != IR_ICMP)
        verr(v, 7,
             "'bounds' on '%s'; only integer comparisons carry "
             "bounds-check provenance",
             ir_op_name((IrOp)in->op));
    if (in->flags & (u8) ~(IRF_VOLATILE | IRF_SEQ_CST | IRF_CALL_VARIADIC |
                           IRF_NSW | IRF_BOUNDS_CHECK | IRF_NORETURN |
                           IRF_SELF_INIT | IRF_FLOW_PROVENANCE))
        verr(v, 7, "unknown flag bits 0x%x", in->flags);

    /* 8: alignment discipline. */
    if (is_mem) {
        if (!pow2_nonzero(in->align)) {
            verr(v, 8, "'%s' alignment %u is not a nonzero power of two",
                 ir_op_name((IrOp)in->op), in->align);
        } else if (in->op == IR_LOAD || in->op == IR_STORE) {
            u8 vt = in->op == IR_LOAD ? in->type : in->ops[0].type;

            /* IR-C-11: ordinary under-alignment is honest for packed
             * objects, but a scalar seq_cst access is only indivisible when
             * it carries the type's natural-alignment guarantee. Both
             * backends rely on that guarantee when selecting machine atomic
             * loads and stores. */
            if ((in->flags & IRF_SEQ_CST) && !ir_type_is_vector((IrType)vt) &&
                in->align != natural_align(vt))
                verr(v, 8,
                     "'%s' of %s with seq_cst requires natural alignment "
                     "%u, got alignment %u",
                     ir_op_name((IrOp)in->op), ir_type_name((IrType)vt),
                     natural_align(vt), in->align);
            else if (in->align > natural_align(vt))
                verr(v, 8,
                     "'%s' of %s claims alignment %u above the "
                     "natural %u",
                     ir_op_name((IrOp)in->op), ir_type_name((IrType)vt),
                     in->align, natural_align(vt));
        }
    }

    /* 9: reference ranges; internal call arity and types. */
    for (i = 0; i < in->nops; i++) {
        if (ir_type_is_vector((IrType)in->ops[i].type) &&
            in->ops[i].kind != IROP_VALUE && in->ops[i].kind != IROP_UNDEF)
            verr(v, 4, "vector operands must be SSA values or undef");
        if (in->ops[i].kind == IROP_SYMBOL && in->ops[i].sym >= v->m->nsyms)
            verr(v, 9, "operand references symbol %u; module has %u",
                 in->ops[i].sym, v->m->nsyms);
    }
    if (in->op == IR_CALL) {
        bool seen_anon = false;

        if (ir_type_is_vector((IrType)in->type))
            verr(v, 4, "vector call results have no Sprint 36 ABI");
        for (i = in->subop == FUNCREF_INDIRECT ? 1u : 0u; i < in->nops; i++)
            if (ir_type_is_vector((IrType)in->ops[i].type))
                verr(v, 4, "vector call arguments have no Sprint 36 ABI");
        /* The anonymous parameters are a SUFFIX of the argument list, and
         * only a variadic callee has any. A gap would mean the boundary was
         * computed per-argument rather than once. */
        for (i = in->subop == FUNCREF_INDIRECT ? 1u : 0u; i < in->nops; i++) {
            bool anon = (in->ops[i].argflags & IROPF_ANON) != 0;
            bool even = (in->ops[i].kind == IROP_VALUE ||
                         in->ops[i].kind == IROP_SYMBOL) &&
                        ir_abi_even_gpr(in->ops[i].b);
            bool stack_align16 = (in->ops[i].kind == IROP_VALUE ||
                                  in->ops[i].kind == IROP_SYMBOL) &&
                                 ir_abi_stack_align16(in->ops[i].b);

            if (anon && !(in->flags & IRF_CALL_VARIADIC))
                verr(v, 9, "arg %u is 'anon' on a call that is not marked 'va'",
                     i);
            if (seen_anon && !anon)
                verr(v, 9,
                     "arg %u follows an 'anon' argument without being one", i);
            seen_anon = seen_anon || anon;
            if ((in->ops[i].argflags & IROPF_SEXT) &&
                (in->ops[i].argflags & IROPF_ZEXT))
                verr(v, 9, "arg %u is both 'sext' and 'zext'", i);
            /* The widening duty exists only for a type NARROWER than the
             * 32-bit unit an argument register holds. */
            if ((in->ops[i].argflags & (IROPF_SEXT | IROPF_ZEXT)) &&
                ir_type_size((IrType)in->ops[i].type) >= 4)
                verr(v, 9, "arg %u is %u bytes wide and cannot need widening",
                     i, ir_type_size((IrType)in->ops[i].type));
            if (even && in->ops[i].type != IRT_I64)
                verr(v, 9,
                     "arg %u is marked even-GPR but is not an i64 ABI leaf", i);
            if (even && (in->ops[i].argflags & IROPF_ONSTACK))
                verr(v, 9, "arg %u is both onstack and even-GPR", i);
            if (stack_align16 && !(in->ops[i].argflags & IROPF_ONSTACK))
                verr(v, 9, "arg %u is stack-align16 but is not marked onstack",
                     i);
            if (stack_align16 && in->ops[i].type != IRT_I64 &&
                in->ops[i].type != IRT_F64)
                verr(v, 9,
                     "arg %u is stack-align16 but is not an eightbyte ABI leaf",
                     i);
            if (stack_align16 && even)
                verr(v, 9, "arg %u is both stack-align16 and even-GPR", i);
            if (stack_align16 && (i + 1u >= in->nops ||
                                  !(in->ops[i + 1u].argflags & IROPF_ONSTACK)))
                verr(v, 9,
                     "arg %u is stack-align16 but is not the first leaf of a "
                     "stacked composite",
                     i);
        }
        if (in->subop == FUNCREF_INTERNAL) {
            if (in->callee >= v->m->nfuncs) {
                verr(v, 9, "call to internal function %u; module has %u",
                     in->callee, v->m->nfuncs);
            } else {
                const IrFunc *cf = &v->m->funcs[in->callee];
                bool unprototyped = cf->unprototyped ||
                                    (in->flags & IRF_CALL_UNPROTOTYPED) != 0;

                /* An old-style definition, or a call formed through an
                 * earlier no-prototype declaration, has no fixed call-site
                 * contract: count and default-promoted operand types are
                 * intentionally unconstrained. A hidden return pointer
                 * remains an ABI invariant and is checked below. */
                if (!unprototyped && (cf->variadic ? in->nops < cf->nparams
                                                   : in->nops != cf->nparams))
                    verr(v, 9, "call to @%s passes %u args; it takes %s%u",
                         cf->name, in->nops, cf->variadic ? "at least " : "",
                         cf->nparams);
                else {
                    u32 checked =
                        unprototyped ? (cf->abi_ret != IR_ABIRET_NONE ? 1u : 0u)
                                     : cf->nparams;

                    if (checked && in->nops == 0) {
                        verr(v, 9,
                             "call to @%s omits its hidden return pointer",
                             cf->name);
                        checked = 0;
                    }
                    for (i = 0; i < checked; i++) {
                        u32 got_kind = in->ops[i].kind == IROP_VALUE ||
                                               in->ops[i].kind == IROP_SYMBOL
                                           ? ir_arg_kind(in->ops[i].b)
                                           : IR_ARG_NONE;
                        u32 want_kind = IR_ARG_NONE;
                        bool got_even = (in->ops[i].kind == IROP_VALUE ||
                                         in->ops[i].kind == IROP_SYMBOL) &&
                                        ir_abi_even_gpr(in->ops[i].b);
                        bool want_even = cf->param_annots &&
                                         ir_abi_even_gpr(cf->param_annots[i]);
                        bool got_stack_align16 =
                            (in->ops[i].kind == IROP_VALUE ||
                             in->ops[i].kind == IROP_SYMBOL) &&
                            ir_abi_stack_align16(in->ops[i].b);
                        bool want_stack_align16 =
                            cf->param_annots &&
                            ir_abi_stack_align16(cf->param_annots[i]);
                        bool got_onstack =
                            (in->ops[i].argflags & IROPF_ONSTACK) != 0;
                        bool want_onstack =
                            cf->param_annots &&
                            ir_param_is_onstack(cf->param_annots[i]);

                        if (in->ops[i].type != cf->param_types[i])
                            verr(v, 9,
                                 "call to @%s: arg %u is %s, "
                                 "parameter is %s",
                                 cf->name, i,
                                 ir_type_name((IrType)in->ops[i].type),
                                 ir_type_name((IrType)cf->param_types[i]));
                        if (i == 0 && cf->abi_ret >= IR_ABIRET_HFA_F32) {
                            /* The HFA IrAbiRet values (f32/f64/f128 leaves)
                             * share ONE argument kind, so the enums stop
                             * running in parallel here and the offset
                             * arithmetic below would compute the wrong kind.
                             */
                            want_kind = IR_ARG_HFA;
                        } else if (i == 0 && cf->abi_ret != IR_ABIRET_NONE) {
                            want_kind =
                                IR_ARG_SRET + (cf->abi_ret - IR_ABIRET_SRET);
                        } else if (cf->param_annots)
                            want_kind = ir_arg_kind(cf->param_annots[i]);
                        if (got_kind != want_kind)
                            verr(v, 9,
                                 "call to @%s: arg %u ABI annotation kind "
                                 "%u does not match %u",
                                 cf->name, i, got_kind, want_kind);
                        else if (want_kind == IR_ARG_BYVAL &&
                                 ir_arg_size(in->ops[i].b) !=
                                     ir_arg_size(cf->param_annots[i]))
                            verr(v, 9,
                                 "call to @%s: arg %u byval size %u does "
                                 "not match %u",
                                 cf->name, i, ir_arg_size(in->ops[i].b),
                                 ir_arg_size(cf->param_annots[i]));
                        if (got_even != want_even)
                            verr(v, 9,
                                 "call to @%s: arg %u even-GPR ABI marker "
                                 "does not match its parameter",
                                 cf->name, i);
                        if (got_stack_align16 != want_stack_align16)
                            verr(v, 9,
                                 "call to @%s: arg %u stack-align16 ABI marker "
                                 "does not match its parameter",
                                 cf->name, i);
                        if (got_onstack != want_onstack)
                            verr(v, 9,
                                 "call to @%s: arg %u onstack ABI marker does "
                                 "not match its parameter",
                                 cf->name, i);
                    }
                }
                if (in->type != cf->ret)
                    verr(v, 9,
                         "call to @%s: result type %s, function "
                         "returns %s",
                         cf->name, ir_type_name((IrType)in->type),
                         ir_type_name((IrType)cf->ret));
            }
        } else if (in->subop == FUNCREF_EXTERNAL) {
            if (in->callee >= v->m->nsyms)
                verr(v, 9, "call references symbol %u; module has %u",
                     in->callee, v->m->nsyms);
        } else if (in->subop == FUNCREF_INDIRECT) {
            if (in->nops < 1 || in->ops[0].type != IRT_PTR)
                verr(v, 9,
                     "indirect call needs a ptr callee as its "
                     "first operand");
        } else {
            verr(v, 9, "bad FuncRef kind %u", in->subop);
        }
    }
}

/* --- per-function driver -------------------------------------------------- */

static void verify_func(V *v, const IrFunc *f)
{
    u32 bi;
    u32 i;

    v->f = f;
    v->dom = ir_domtree_build(v->scratch, f);
    v->reach = arena_alloc(
        v->scratch, f->nblocks ? f->nblocks * sizeof(bool) : 1, _Alignof(bool));
    for (bi = 0; bi < f->nblocks; bi++) {
        BlockId b = {bi + 1};

        /* The entry dominates exactly the reachable blocks (every path
         * starts there), and ir_dominates answers false for blocks the
         * dom tree never numbered — so this doubles as the reachability
         * probe for check 6. */
        v->reach[bi] = bi == 0 || ir_dominates(v->dom, (BlockId){1}, b);
    }
    if (f->nblocks == 0) {
        v->blk_name = NULL;
        verr(v, 3, "function has no blocks");
        v->f = NULL;
        return;
    }
    for (i = 0; i < f->nparams; i++) {
        u64 annot = f->param_annots ? f->param_annots[i] : 0;

        if (ir_param_is_restrict(annot) && f->param_types[i] != IRT_PTR)
            verr(v, 4, "parameter %u is marked restrict but is not ptr", i);
        if (ir_abi_even_gpr(annot) && f->param_types[i] != IRT_I64)
            verr(v, 4,
                 "parameter %u is marked even-GPR but is not an i64 ABI "
                 "leaf",
                 i);
        if (ir_abi_even_gpr(annot) && ir_param_is_onstack(annot))
            verr(v, 4, "parameter %u is both onstack and even-GPR", i);
        if (ir_abi_stack_align16(annot) && !ir_param_is_onstack(annot))
            verr(v, 4,
                 "parameter %u is stack-align16 but is not marked onstack", i);
        if (ir_abi_stack_align16(annot) && f->param_types[i] != IRT_I64 &&
            f->param_types[i] != IRT_F64)
            verr(v, 4,
                 "parameter %u is stack-align16 but is not an eightbyte ABI "
                 "leaf",
                 i);
        if (ir_abi_stack_align16(annot) && ir_abi_even_gpr(annot))
            verr(v, 4, "parameter %u is both stack-align16 and even-GPR", i);
        if (ir_abi_stack_align16(annot) &&
            (i + 1u >= f->nparams || !f->param_annots ||
             !ir_param_is_onstack(f->param_annots[i + 1u])))
            verr(v, 4,
                 "parameter %u is stack-align16 but is not the first leaf of "
                 "a stacked composite",
                 i);
        if (ir_type_is_vector((IrType)f->param_types[i]))
            verr(v, 4, "vector function parameters have no Sprint 36 ABI");
    }
    if (ir_type_is_vector((IrType)f->ret))
        verr(v, 4, "vector function returns have no Sprint 36 ABI");
    if (f->abi_ret != IR_ABIRET_NONE &&
        (f->nparams == 0 || f->param_types[0] != IRT_PTR ||
         f->ret != IRT_VOID)) {
        v->blk_name = NULL;
        verr(v, 4,
             "aggregate ABI return requires void IR return and hidden ptr "
             "parameter 0");
    }

    /* 5: entry shape. */
    v->blk_name = f->blocks[0].name;
    if (f->blocks[0].nparams != 0)
        verr(v, 5, "the entry block cannot declare parameters");
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            for (i = 0; i < in->nedges; i++)
                if (in->edges[i].target.v == 1) {
                    v->blk_name = f->blocks[bi].name;
                    verr(v, 5,
                         "the entry block cannot have predecessors "
                         "(branch found here)");
                }
    }

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrBlock *blk = &f->blocks[bi];
        BlockId bid = {bi + 1};
        const IrInst *in;
        u32 pos;
        u32 nterm = 0;

        v->blk = blk;
        v->blk_name = blk->name;

        /* 6: orphans rejected. */
        if (!v->reach[bi]) {
            verr(v, 6,
                 "block is unreachable from the entry; passes must "
                 "delete dead blocks, not abandon them");
            continue; /* dominance is meaningless here; skip use checks */
        }

        /* 3: exactly one terminator, last. */
        if (!blk->first) {
            verr(v, 3, "empty block; every block ends in a terminator");
            continue;
        }
        for (in = blk->first, pos = 0; in; in = in->next, pos++) {
            if (is_terminator(in->op)) {
                nterm++;
                if (in->next)
                    verr(v, 3,
                         "terminator '%s' is not the last "
                         "instruction",
                         ir_op_name((IrOp)in->op));
            }
        }
        if (nterm == 0)
            verr(v, 3, "block has no terminator");
        else if (nterm > 1)
            verr(v, 3, "block has %u terminators", nterm);

        for (in = blk->first, pos = 0; in; in = in->next, pos++) {
            /* 10: reserved opcodes absent. */
            if (in->op >= IR_VA_ARG) {
                verr(v, 10,
                     "reserved opcode %u present; its sprint has "
                     "not landed",
                     in->op);
                continue;
            }
            /* 1: dominance of every value use. */
            for (i = 0; i < in->nops; i++)
                check_use(v, &in->ops[i], bid, pos);
            for (i = 0; i < in->nedges; i++) {
                u32 j;

                for (j = 0; j < in->edges[i].nargs; j++)
                    check_use(v, &in->edges[i].args[j], bid, pos);
            }
            /* 2: edge arity/types. */
            for (i = 0; i < in->nedges; i++) {
                const IrEdge *e = &in->edges[i];
                const IrBlock *tgt = ir_block((IrFunc *)f, e->target);
                u32 j;

                if (!tgt) {
                    verr(v, 2,
                         "branch to block id %u, which does not "
                         "exist",
                         e->target.v);
                    continue;
                }
                if (e->nargs != tgt->nparams) {
                    verr(v, 2,
                         "edge to '%s' passes %u args; the block "
                         "declares %u params",
                         tgt->name ? tgt->name : "?", e->nargs, tgt->nparams);
                    continue;
                }
                for (j = 0; j < e->nargs; j++) {
                    u8 want = f->vals[tgt->params[j].v - 1].type;

                    if (ir_type_is_vector((IrType)e->args[j].type) &&
                        e->args[j].kind != IROP_VALUE &&
                        e->args[j].kind != IROP_UNDEF)
                        verr(v, 4,
                             "vector edge arguments must be SSA values or "
                             "undef");
                    if (e->args[j].type != want)
                        verr(v, 2,
                             "edge to '%s': arg %u is %s, param "
                             "is %s",
                             tgt->name ? tgt->name : "?", j,
                             ir_type_name((IrType)e->args[j].type),
                             ir_type_name((IrType)want));
                }
            }
            /* 4, 7, 8, 9. */
            check_inst_types(v, in);
            check_inst_misc(v, in);
        }
    }
    v->blk = NULL;
    v->blk_name = NULL;
    v->f = NULL;
}

static void check_setjmp_flag(V *v, const IrFunc *f)
{
    bool found = false;
    u32 bi;

    for (bi = 0; bi < f->nblocks && !found; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in && !found; in = in->next)
            if (in->op == IR_CALL && in->subop == FUNCREF_EXTERNAL &&
                in->callee < v->m->nsyms &&
                ir_name_is_returns_twice(v->m->syms[in->callee]))
                found = true;
    }
    if (found != f->calls_setjmp) {
        v->f = (const IrFunc *)f;
        v->blk_name = NULL;
        verr(v, 11,
             found ? "function calls setjmp but is not marked 'setjmp'"
                   : "function is marked 'setjmp' but never calls it");
        v->f = NULL;
    }
}

u32 ir_count_volatile_ops(const IrFunc *f)
{
    u32 n = 0;
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->flags & IRF_VOLATILE)
                n++;
    }
    return n;
}

void ir_snapshot_volatile(const IrModule *m, u32 *out)
{
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        out[i] = ir_count_volatile_ops(&m->funcs[i]);
}

bool ir_volatile_counts_match(const IrModule *m, const u32 *before,
                              u32 *bad_func)
{
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        if (ir_count_volatile_ops(&m->funcs[i]) != before[i]) {
            if (bad_func)
                *bad_func = i;
            return false;
        }
    return true;
}

void ir_snapshot_volatile_order(Arena *arena, const IrModule *m,
                                IrVolatileSnapshot *out)
{
    u32 fi;

    for (fi = 0; fi < m->nfuncs; fi++) {
        const IrFunc *f = &m->funcs[fi];
        u32 n = 0;
        const IrInst **ops = NULL;
        u32 *blocks = NULL;
        u8 *precedes = NULL;
        u32 bi, at = 0;

        for (bi = 0; bi < f->nblocks; bi++) {
            const IrInst *in;

            for (in = f->blocks[bi].first; in; in = in->next)
                if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST))
                    n++;
        }
        out[fi].func_name = f->name;
        if (n) {
            ops = arena_alloc(arena, n * sizeof(*ops), _Alignof(IrInst *));
            blocks = arena_alloc(arena, n * sizeof(*blocks), _Alignof(u32));
            precedes = arena_alloc(arena, (size_t)n * n, _Alignof(u8));
            memset(precedes, 0, (size_t)n * n);
        }
        for (bi = 0; bi < f->nblocks; bi++) {
            const IrInst *in;

            for (in = f->blocks[bi].first; in; in = in->next)
                if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) {
                    ops[at++] = in;
                    blocks[at - 1] = bi;
                }
        }
        if (n) {
            IrDomTree *dom = ir_domtree_build(arena, f);
            u32 i, j;

            for (i = 0; i < n; i++)
                for (j = 0; j < n; j++)
                    if (i != j && ((blocks[i] == blocks[j] && i < j) ||
                                   (blocks[i] != blocks[j] &&
                                    ir_dominates(dom, (BlockId){blocks[i] + 1},
                                                 (BlockId){blocks[j] + 1}))))
                        precedes[(size_t)i * n + j] = 1;
        }
        out[fi].ops = ops;
        out[fi].precedes = precedes;
        out[fi].nops = n;
        out[fi].inline_group_count = m->ninline_pinned_groups;
    }
    out[m->nfuncs].func_name = NULL;
    out[m->nfuncs].ops = NULL;
    out[m->nfuncs].precedes = NULL;
    out[m->nfuncs].nops = 0;
    out[m->nfuncs].inline_group_count = m->ninline_pinned_groups;
}

static bool find_inst_position(const IrFunc *f, const IrInst *needle,
                               u32 *block, u32 *position)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;
        u32 pos = 0;

        for (in = f->blocks[bi].first; in; in = in->next, pos++)
            if (in == needle) {
                *block = bi;
                *position = pos;
                return true;
            }
    }
    return false;
}

static bool snapshot_order_preserved(Arena *scratch, const IrFunc *f,
                                     const IrVolatileSnapshot *snapshot)
{
    IrDomTree *dom;
    u32 *blocks, *positions;
    u32 i, j;

    if (!snapshot->nops)
        return true;
    blocks =
        arena_alloc(scratch, snapshot->nops * sizeof(*blocks), _Alignof(u32));
    positions = arena_alloc(scratch, snapshot->nops * sizeof(*positions),
                            _Alignof(u32));
    for (i = 0; i < snapshot->nops; i++)
        if (!find_inst_position(f, snapshot->ops[i], &blocks[i], &positions[i]))
            return false;
    dom = ir_domtree_build(scratch, f);
    for (i = 0; i < snapshot->nops; i++)
        for (j = 0; j < snapshot->nops; j++) {
            bool still_precedes;

            if (!snapshot->precedes[(size_t)i * snapshot->nops + j])
                continue;
            still_precedes = blocks[i] == blocks[j]
                                 ? positions[i] < positions[j]
                                 : ir_dominates(dom, (BlockId){blocks[i] + 1},
                                                (BlockId){blocks[j] + 1});
            if (!still_precedes)
                return false;
        }
    return true;
}

static u32 pinned_count(const IrFunc *f)
{
    u32 bi, n = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            n += !!(in->flags & (IRF_VOLATILE | IRF_SEQ_CST));
    }
    return n;
}

static const IrFunc *find_func_named(const IrModule *m, const char *name,
                                     u32 *index)
{
    u32 fi;

    for (fi = 0; fi < m->nfuncs; fi++)
        if (strcmp(m->funcs[fi].name, name) == 0) {
            if (index)
                *index = fi;
            return &m->funcs[fi];
        }
    if (index)
        *index = m->nfuncs;
    return NULL;
}

static const IrVolatileSnapshot *find_snapshot(const IrVolatileSnapshot *before,
                                               const char *name)
{
    u32 si;

    for (si = 0; before[si].func_name; si++)
        if (strcmp(before[si].func_name, name) == 0)
            return &before[si];
    return NULL;
}

static bool pinned_order_matches(const IrModule *m,
                                 const IrVolatileSnapshot *before,
                                 bool allow_removed_funcs, u32 *bad_func)
{
    Arena scratch;
    u32 fi, si;

    arena_init(&scratch);
    for (si = 0; before[si].func_name; si++) {
        const IrFunc *f;

        if (!before[si].nops)
            continue;
        f = find_func_named(m, before[si].func_name, &fi);
        if (!f && allow_removed_funcs)
            continue;
        if (!f || pinned_count(f) != before[si].nops)
            goto mismatch;
        if (!snapshot_order_preserved(&scratch, f, &before[si]))
            goto mismatch;
    }
    /* A newly introduced volatile-bearing function has no snapshot row. */
    for (fi = 0; fi < m->nfuncs; fi++) {
        if (!pinned_count(&m->funcs[fi]))
            continue;
        if (!find_snapshot(before, m->funcs[fi].name))
            goto mismatch;
    }
    arena_free_all(&scratch);
    return true;

mismatch:
    arena_free_all(&scratch);
    if (bad_func)
        *bad_func = fi < m->nfuncs ? fi : m->nfuncs;
    return false;
}

bool ir_volatile_order_matches(const IrModule *m,
                               const IrVolatileSnapshot *before, u32 *bad_func)
{
    return pinned_order_matches(m, before, false, bad_func);
}

bool ir_pinned_delete_funcs_matches(const IrModule *m,
                                    const IrVolatileSnapshot *before,
                                    u32 *bad_func)
{
    return pinned_order_matches(m, before, true, bad_func);
}

static bool pinned_metadata_eq(const IrInst *a, const IrInst *b)
{
    return a->op == b->op && a->type == b->type && a->subop == b->subop &&
           a->flags == b->flags && a->align == b->align && a->loc == b->loc &&
           a->nops == b->nops && a->nedges == b->nedges;
}

static bool snapshot_has_pointer(const IrVolatileSnapshot *before,
                                 const IrInst *needle)
{
    u32 si, oi;

    for (si = 0; before[si].func_name; si++)
        for (oi = 0; oi < before[si].nops; oi++)
            if (before[si].ops[oi] == needle)
                return true;
    return false;
}

static bool snapshot_has_metadata(const IrVolatileSnapshot *before,
                                  const IrInst *needle)
{
    u32 si, oi;

    for (si = 0; before[si].func_name; si++)
        for (oi = 0; oi < before[si].nops; oi++)
            if (pinned_metadata_eq(before[si].ops[oi], needle))
                return true;
    return false;
}

static bool inst_precedes(const IrDomTree *dom, u32 ablock, u32 apos,
                          u32 bblock, u32 bpos)
{
    return ablock == bblock ? apos < bpos
                            : ir_dominates(dom, (BlockId){ablock + 1},
                                           (BlockId){bblock + 1});
}

void ir_capture_inline_pinned_plan(IrModule *m, const IrFunc *caller,
                                   const IrInst *call, const IrFunc *source,
                                   IrInlinePinnedPlan *out)
{
    Arena scratch;
    IrDomTree *dom;
    u32 call_block, call_position;
    u32 bi, oi = 0, ai = 0;

    memset(out, 0, sizeof(*out));
    out->nops = pinned_count(source);
    out->nanchors = pinned_count(caller);
    if (!out->nops)
        return;
    out->sources =
        arena_alloc(m->arena, (size_t)out->nops * sizeof(*out->sources),
                    _Alignof(IrInst *));
    if (out->nanchors) {
        out->anchors =
            arena_alloc(m->arena, (size_t)out->nanchors * sizeof(*out->anchors),
                        _Alignof(IrInst *));
        out->anchor_precedes_call = arena_alloc(
            m->arena,
            (size_t)out->nanchors * sizeof(*out->anchor_precedes_call),
            _Alignof(bool));
        out->call_precedes_anchor = arena_alloc(
            m->arena,
            (size_t)out->nanchors * sizeof(*out->call_precedes_anchor),
            _Alignof(bool));
    }
    if (!find_inst_position(caller, call, &call_block, &call_position))
        CGF_ICE("inline: call site disappeared before pinned capture");
    arena_init(&scratch);
    dom = ir_domtree_build(&scratch, caller);
    for (bi = 0; bi < caller->nblocks; bi++) {
        const IrInst *in;
        u32 position = 0;

        for (in = caller->blocks[bi].first; in; in = in->next, position++)
            if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) {
                out->anchors[ai] = in;
                out->anchor_precedes_call[ai] =
                    inst_precedes(dom, bi, position, call_block, call_position);
                out->call_precedes_anchor[ai] =
                    inst_precedes(dom, call_block, call_position, bi, position);
                ai++;
            }
    }
    for (bi = 0; bi < source->nblocks; bi++) {
        const IrInst *in;

        for (in = source->blocks[bi].first; in; in = in->next)
            if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST))
                out->sources[oi++] = in;
    }
    if (ai != out->nanchors || oi != out->nops)
        CGF_ICE("inline: pinned capture count disagrees with CFG");
    arena_free_all(&scratch);
}

void ir_record_inline_pinned_group(IrModule *m, IrFunc *caller,
                                   const IrFunc *source,
                                   const IrInlinePinnedPlan *plan,
                                   IrInst **clones, u32 nops)
{
    Arena scratch;
    IrInlinePinnedGroup *group;
    IrDomTree *source_dom;
    u32 *source_blocks, *source_positions;
    u32 i, j;

    if (!nops)
        return;
    if (m->ninline_pinned_groups == m->cap_inline_pinned_groups) {
        u32 oldcap = m->cap_inline_pinned_groups;
        u32 newcap = oldcap ? oldcap * 2 : 8;
        IrInlinePinnedGroup *next =
            arena_alloc(m->arena, (size_t)newcap * sizeof(*next),
                        _Alignof(IrInlinePinnedGroup));

        if (m->ninline_pinned_groups)
            memcpy(next, m->inline_pinned_groups,
                   (size_t)m->ninline_pinned_groups * sizeof(*next));
        m->inline_pinned_groups = next;
        m->cap_inline_pinned_groups = newcap;
    }
    group = &m->inline_pinned_groups[m->ninline_pinned_groups++];
    memset(group, 0, sizeof(*group));
    group->caller_name = caller->name;
    group->source_name = source->name;
    if (nops != plan->nops)
        CGF_ICE("inline: pinned clone count disagrees with capture");
    group->nops = nops;
    group->sources = arena_alloc(
        m->arena, (size_t)nops * sizeof(*plan->sources), _Alignof(IrInst *));
    group->clones = arena_alloc(m->arena, (size_t)nops * sizeof(*clones),
                                _Alignof(IrInst *));
    group->precedes = arena_alloc(m->arena, (size_t)nops * nops, _Alignof(u8));
    memcpy((void *)group->sources, plan->sources,
           (size_t)nops * sizeof(*plan->sources));
    memcpy(group->clones, clones, (size_t)nops * sizeof(*clones));
    memset(group->precedes, 0, (size_t)nops * nops);

    group->nanchors = plan->nanchors;
    if (plan->nanchors) {
        group->anchors = arena_alloc(
            m->arena, (size_t)plan->nanchors * sizeof(*group->anchors),
            _Alignof(IrInst *));
        group->anchor_precedes =
            arena_alloc(m->arena, (size_t)plan->nanchors * nops, _Alignof(u8));
        group->clone_precedes =
            arena_alloc(m->arena, (size_t)nops * plan->nanchors, _Alignof(u8));
        memset(group->anchor_precedes, 0, (size_t)plan->nanchors * nops);
        memset(group->clone_precedes, 0, (size_t)nops * plan->nanchors);
        memcpy((void *)group->anchors, plan->anchors,
               (size_t)plan->nanchors * sizeof(*plan->anchors));
    }

    arena_init(&scratch);
    source_blocks = arena_alloc(&scratch, (size_t)nops * sizeof(*source_blocks),
                                _Alignof(u32));
    source_positions = arena_alloc(
        &scratch, (size_t)nops * sizeof(*source_positions), _Alignof(u32));
    for (i = 0; i < nops; i++)
        if (!find_inst_position(source, plan->sources[i], &source_blocks[i],
                                &source_positions[i]))
            CGF_ICE("inline: pinned clone provenance lost during splice");
    source_dom = ir_domtree_build(&scratch, source);
    for (i = 0; i < nops; i++) {
        bool source_dominates_returns = true;

        for (j = 0; j < nops; j++)
            if (i != j &&
                inst_precedes(source_dom, source_blocks[i], source_positions[i],
                              source_blocks[j], source_positions[j]))
                group->precedes[(size_t)i * nops + j] = 1;
        for (j = 0; j < source->nblocks; j++) {
            const IrInst *ret = source->blocks[j].last;
            u32 ret_block, ret_position;

            if (!ret || ret->op != IR_RET)
                continue;
            if (!find_inst_position(source, ret, &ret_block, &ret_position) ||
                !inst_precedes(source_dom, source_blocks[i],
                               source_positions[i], ret_block, ret_position))
                source_dominates_returns = false;
        }
        for (j = 0; j < plan->nanchors; j++) {
            if (plan->anchor_precedes_call[j])
                group->anchor_precedes[(size_t)j * nops + i] = 1;
            if (plan->call_precedes_anchor[j] && source_dominates_returns)
                group->clone_precedes[(size_t)i * plan->nanchors + j] = 1;
        }
    }
    arena_free_all(&scratch);
}

static bool recorded_clone_before(const IrModule *m, u32 first_group,
                                  u32 end_group, const IrInst *needle)
{
    u32 gi, oi;

    for (gi = first_group; gi < end_group; gi++)
        for (oi = 0; oi < m->inline_pinned_groups[gi].nops; oi++)
            if (m->inline_pinned_groups[gi].clones[oi] == needle)
                return true;
    return false;
}

static u32 recorded_clone_count(const IrModule *m, u32 first_group,
                                const IrInst *needle)
{
    u32 gi, oi, count = 0;

    for (gi = first_group; gi < m->ninline_pinned_groups; gi++)
        for (oi = 0; oi < m->inline_pinned_groups[gi].nops; oi++)
            count += m->inline_pinned_groups[gi].clones[oi] == needle;
    return count;
}

static u32 inline_expected_anchor_count(const IrModule *m,
                                        const IrVolatileSnapshot *before,
                                        u32 first_group, u32 gi,
                                        const char *caller_name)
{
    const IrVolatileSnapshot *snapshot = find_snapshot(before, caller_name);
    u32 count = snapshot ? snapshot->nops : 0;

    for (; first_group < gi; first_group++)
        if (strcmp(m->inline_pinned_groups[first_group].caller_name,
                   caller_name) == 0)
            count += m->inline_pinned_groups[first_group].nops;
    return count;
}

static bool inline_is_expected_anchor(const IrModule *m,
                                      const IrVolatileSnapshot *before,
                                      u32 first_group, u32 gi,
                                      const char *caller_name,
                                      const IrInst *needle)
{
    const IrVolatileSnapshot *snapshot = find_snapshot(before, caller_name);
    u32 oi;

    if (snapshot)
        for (oi = 0; oi < snapshot->nops; oi++)
            if (snapshot->ops[oi] == needle)
                return true;
    for (; first_group < gi; first_group++) {
        const IrInlinePinnedGroup *prior =
            &m->inline_pinned_groups[first_group];

        if (strcmp(prior->caller_name, caller_name) != 0)
            continue;
        for (oi = 0; oi < prior->nops; oi++)
            if (prior->clones[oi] == needle)
                return true;
    }
    return false;
}

static u32 inline_anchor_occurrences(const IrInlinePinnedGroup *group,
                                     const IrInst *needle)
{
    u32 i, count = 0;

    for (i = 0; i < group->nanchors; i++)
        count += group->anchors[i] == needle;
    return count;
}

static bool inline_group_matches(Arena *scratch, const IrModule *m,
                                 const IrVolatileSnapshot *before,
                                 u32 first_group, u32 gi, u32 *bad_func)
{
    const IrInlinePinnedGroup *group = &m->inline_pinned_groups[gi];
    const IrFunc *caller, *source;
    IrDomTree *dom;
    u32 *clone_blocks, *clone_positions, *anchor_blocks, *anchor_positions;
    u32 fi, i, j;

    caller = find_func_named(m, group->caller_name, &fi);
    if (!caller || !group->nops)
        goto mismatch;
    source = find_func_named(m, group->source_name, NULL);
    if (!source || pinned_count(source) != group->nops)
        goto mismatch;
    if (group->nanchors != inline_expected_anchor_count(m, before, first_group,
                                                        gi, group->caller_name))
        goto mismatch;
    clone_blocks = arena_alloc(
        scratch, (size_t)group->nops * sizeof(*clone_blocks), _Alignof(u32));
    clone_positions = arena_alloc(
        scratch, (size_t)group->nops * sizeof(*clone_positions), _Alignof(u32));
    anchor_blocks =
        arena_alloc(scratch,
                    (size_t)(group->nanchors ? group->nanchors : 1) *
                        sizeof(*anchor_blocks),
                    _Alignof(u32));
    anchor_positions =
        arena_alloc(scratch,
                    (size_t)(group->nanchors ? group->nanchors : 1) *
                        sizeof(*anchor_positions),
                    _Alignof(u32));
    for (i = 0; i < group->nops; i++) {
        u32 source_block, source_position;

        if ((!snapshot_has_pointer(before, group->sources[i]) &&
             !recorded_clone_before(m, first_group, gi, group->sources[i])) ||
            !find_inst_position(source, group->sources[i], &source_block,
                                &source_position) ||
            snapshot_has_pointer(before, group->clones[i]) ||
            group->sources[i] == group->clones[i] ||
            recorded_clone_before(m, first_group, gi, group->clones[i]) ||
            !pinned_metadata_eq(group->sources[i], group->clones[i]) ||
            !find_inst_position(caller, group->clones[i], &clone_blocks[i],
                                &clone_positions[i]))
            goto mismatch;
        for (j = 0; j < i; j++)
            if (group->sources[j] == group->sources[i] ||
                group->clones[j] == group->clones[i])
                goto mismatch;
    }
    for (i = 0; i < group->nanchors; i++)
        if (!inline_is_expected_anchor(m, before, first_group, gi,
                                       group->caller_name, group->anchors[i]) ||
            inline_anchor_occurrences(group, group->anchors[i]) != 1 ||
            !find_inst_position(caller, group->anchors[i], &anchor_blocks[i],
                                &anchor_positions[i]))
            goto mismatch;
    dom = ir_domtree_build(scratch, caller);
    for (i = 0; i < group->nops; i++) {
        for (j = 0; j < group->nops; j++)
            if (group->precedes[(size_t)i * group->nops + j] &&
                !inst_precedes(dom, clone_blocks[i], clone_positions[i],
                               clone_blocks[j], clone_positions[j]))
                goto mismatch;
        for (j = 0; j < group->nanchors; j++) {
            if (group->anchor_precedes[(size_t)j * group->nops + i] &&
                !inst_precedes(dom, anchor_blocks[j], anchor_positions[j],
                               clone_blocks[i], clone_positions[i]))
                goto mismatch;
            if (group->clone_precedes[(size_t)i * group->nanchors + j] &&
                !inst_precedes(dom, clone_blocks[i], clone_positions[i],
                               anchor_blocks[j], anchor_positions[j]))
                goto mismatch;
        }
    }
    return true;

mismatch:
    if (bad_func)
        *bad_func = fi < m->nfuncs ? fi : m->nfuncs;
    return false;
}

bool ir_pinned_inline_matches(const IrModule *m,
                              const IrVolatileSnapshot *before, u32 *bad_func)
{
    Arena scratch;
    u32 si, fi = m->nfuncs, bi, snapshot_rows = 0, first_group;

    arena_init(&scratch);
    while (before[snapshot_rows].func_name)
        snapshot_rows++;
    first_group = before[snapshot_rows].inline_group_count;
    if (first_group > m->ninline_pinned_groups)
        goto mismatch;
    /* Every original pinned instruction remains in its original function and
     * preserves the snapshot's dominance/order relation. Inlining may split a
     * block at a call and append its continuation after pre-existing successor
     * blocks, so global block-document order is not an execution-order
     * invariant. */
    for (si = 0; before[si].func_name; si++) {
        const IrFunc *f = find_func_named(m, before[si].func_name, &fi);
        u32 oi;

        if (!f)
            goto mismatch;
        for (oi = 0; oi < before[si].nops; oi++) {
            u32 found = 0;

            for (bi = 0; bi < f->nblocks; bi++) {
                const IrInst *in;

                for (in = f->blocks[bi].first; in; in = in->next)
                    found += in == before[si].ops[oi];
            }
            if (found != 1)
                goto mismatch;
        }
        if (!snapshot_order_preserved(&scratch, f, &before[si]))
            goto mismatch;
    }
    for (si = first_group; si < m->ninline_pinned_groups; si++)
        if (!inline_group_matches(&scratch, m, before, first_group, si,
                                  bad_func)) {
            arena_free_all(&scratch);
            return false;
        }
    /* Every new pinned instruction belongs to exactly one recorded clone
     * group; metadata coincidence is not provenance. */
    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            const IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next) {
                if (!(in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) ||
                    snapshot_has_pointer(before, in))
                    continue;
                if (recorded_clone_count(m, first_group, in) != 1)
                    goto mismatch;
            }
        }
    arena_free_all(&scratch);
    return true;

mismatch:
    arena_free_all(&scratch);
    if (bad_func)
        *bad_func = fi < m->nfuncs ? fi : m->nfuncs;
    return false;
}

bool ir_pinned_metadata_clones_match(const IrModule *m,
                                     const IrVolatileSnapshot *before,
                                     u32 *bad_func)
{
    Arena scratch;
    u32 si, fi, bi;

    arena_init(&scratch);
    for (si = 0; before[si].func_name; si++) {
        const IrFunc *f = find_func_named(m, before[si].func_name, &fi);
        u32 oi;

        if (!f)
            goto mismatch;
        for (oi = 0; oi < before[si].nops; oi++) {
            u32 found = 0;

            for (bi = 0; bi < f->nblocks; bi++) {
                const IrInst *in;

                for (in = f->blocks[bi].first; in; in = in->next)
                    found += in == before[si].ops[oi];
            }
            if (found != 1)
                goto mismatch;
        }
        if (!snapshot_order_preserved(&scratch, f, &before[si]))
            goto mismatch;
    }
    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            const IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next)
                if ((in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) &&
                    !snapshot_has_pointer(before, in) &&
                    !snapshot_has_metadata(before, in))
                    goto mismatch;
        }
    arena_free_all(&scratch);
    return true;

mismatch:
    arena_free_all(&scratch);
    if (bad_func)
        *bad_func = fi < m->nfuncs ? fi : m->nfuncs;
    return false;
}

bool ir_verify(DiagCtx *dc, const IrModule *m)
{
    return ir_verify_report(dc, m, NULL, 0);
}

bool ir_verify_report(DiagCtx *dc, const IrModule *m, char *why, size_t why_cap)
{
    V v;
    Arena scratch;
    u32 i;

    memset(&v, 0, sizeof(v));
    arena_init(&scratch);
    v.dc = dc;
    v.m = m;
    v.scratch = &scratch;
    v.ok = true;
    v.why = why;
    v.why_cap = why_cap;
    if (why && why_cap)
        why[0] = '\0';

    /* Module-level ranges (check 9) and global alignments (check 8). */
    for (i = 0; i < m->nglobals; i++) {
        const IrGlobal *g = &m->globals[i];
        u32 j;

        if (!pow2_nonzero(g->align))
            verr(&v, 8,
                 "global @%s alignment %u is not a nonzero power "
                 "of two",
                 g->name, g->align);
        for (j = 0; j < g->nrelocs; j++) {
            if (g->relocs[j].symbol >= m->nsyms)
                verr(&v, 9,
                     "global @%s reloc %u references symbol %u; "
                     "module has %u",
                     g->name, j, g->relocs[j].symbol, m->nsyms);
            if (g->relocs[j].offset > g->size ||
                g->size - g->relocs[j].offset < 8)
                verr(&v, 9,
                     "global @%s reloc %u at offset %llu does not "
                     "fit in %llu bytes",
                     g->name, j, (unsigned long long)g->relocs[j].offset,
                     (unsigned long long)g->size);
        }
    }
    for (i = 0; i < m->nfuncs; i++) {
        verify_func(&v, &m->funcs[i]);
        check_setjmp_flag(&v, &m->funcs[i]);
    }
    arena_free_all(&scratch);
    return v.ok;
}
