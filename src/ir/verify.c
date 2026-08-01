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
 *   7  volatile/seq_cst flags only on ops that can carry them
 *   8  alignments nonzero powers of two; load/store never over-aligned
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
    static const u32 tab[] = {1, 2, 4, 8, 4, 8, 16, 16, 8};

    return t <= IRT_PTR ? tab[t] : 1;
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
        break;
    case IR_MEMSET:
        if (in->ops[0].type != IRT_PTR || in->ops[1].type != IRT_I32 ||
            in->ops[2].type != IRT_I64)
            verr(v, 4, "'memset' is (ptr dst, i32 byte, i64 size)");
        break;
    case IR_SELECT:
        if (in->ops[0].type != IRT_I32)
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
    if (in->flags & IRF_VOLATILE) {
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
    if (in->flags & (u8) ~(IRF_VOLATILE | IRF_SEQ_CST | IRF_CALL_VARIADIC))
        verr(v, 7, "unknown flag bits 0x%x", in->flags);

    /* 8: alignment discipline. */
    if (is_mem) {
        if (!pow2_nonzero(in->align)) {
            verr(v, 8, "'%s' alignment %u is not a nonzero power of two",
                 ir_op_name((IrOp)in->op), in->align);
        } else if (in->op == IR_LOAD || in->op == IR_STORE) {
            u8 vt = in->op == IR_LOAD ? in->type : in->ops[0].type;

            /* Under-aligned is honest (packed structs); OVER-aligned
             * claims guarantees nobody made — reject the lie. */
            if (in->align > natural_align(vt))
                verr(v, 8,
                     "'%s' of %s claims alignment %u above the "
                     "natural %u",
                     ir_op_name((IrOp)in->op), ir_type_name((IrType)vt),
                     in->align, natural_align(vt));
        }
    }

    /* 9: reference ranges; internal call arity and types. */
    for (i = 0; i < in->nops; i++)
        if (in->ops[i].kind == IROP_SYMBOL && in->ops[i].sym >= v->m->nsyms)
            verr(v, 9, "operand references symbol %u; module has %u",
                 in->ops[i].sym, v->m->nsyms);
    if (in->op == IR_CALL) {
        if (in->subop == FUNCREF_INTERNAL) {
            if (in->callee >= v->m->nfuncs) {
                verr(v, 9, "call to internal function %u; module has %u",
                     in->callee, v->m->nfuncs);
            } else {
                const IrFunc *cf = &v->m->funcs[in->callee];

                /* Variadic callees: at LEAST the named parameters, typed;
                 * the tail is the va machinery's business. */
                if (cf->variadic ? in->nops < cf->nparams
                                 : in->nops != cf->nparams)
                    verr(v, 9, "call to @%s passes %u args; it takes %s%u",
                         cf->name, in->nops, cf->variadic ? "at least " : "",
                         cf->nparams);
                else {
                    for (i = 0; i < cf->nparams; i++)
                        if (in->ops[i].type != cf->param_types[i])
                            verr(v, 9,
                                 "call to @%s: arg %u is %s, "
                                 "parameter is %s",
                                 cf->name, i,
                                 ir_type_name((IrType)in->ops[i].type),
                                 ir_type_name((IrType)cf->param_types[i]));
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

/* Check 11: calls_setjmp is set IFF a setjmp-family call exists. The
 * name set matches lowering's recognizer. */
static bool is_setjmp_name(const char *n)
{
    return n && (strcmp(n, "setjmp") == 0 || strcmp(n, "sigsetjmp") == 0 ||
                 strcmp(n, "_setjmp") == 0);
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
                is_setjmp_name(v->m->syms[in->callee]))
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
        u32 n = ir_count_volatile_ops(f);
        const IrInst **ops = NULL;
        u32 bi, at = 0;

        if (n)
            ops = arena_alloc(arena, n * sizeof(*ops), _Alignof(IrInst *));
        for (bi = 0; bi < f->nblocks; bi++) {
            const IrInst *in;

            for (in = f->blocks[bi].first; in; in = in->next)
                if (in->flags & IRF_VOLATILE)
                    ops[at++] = in;
        }
        out[fi].ops = ops;
        out[fi].nops = n;
    }
}

bool ir_volatile_order_matches(const IrModule *m,
                               const IrVolatileSnapshot *before, u32 *bad_func)
{
    u32 fi;

    for (fi = 0; fi < m->nfuncs; fi++) {
        const IrFunc *f = &m->funcs[fi];
        u32 bi, at = 0;

        if (ir_count_volatile_ops(f) != before[fi].nops)
            goto mismatch;
        for (bi = 0; bi < f->nblocks; bi++) {
            const IrInst *in;

            for (in = f->blocks[bi].first; in; in = in->next) {
                if (!(in->flags & IRF_VOLATILE))
                    continue;
                if (before[fi].ops[at++] != in)
                    goto mismatch;
            }
        }
    }
    return true;

mismatch:
    if (bad_func)
        *bad_func = fi;
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
            if (g->relocs[j].offset + 8 > g->size)
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
