#include "ir/ir.h"

#include <string.h>

/* The deterministic printer. Two prints of one module are byte-identical:
 * everything is emitted from module arrays in index order and values are
 * RENUMBERED %0.. in document order (function params, then per block in
 * layout order: its params, then its instruction results). Document order
 * is exactly the order the parser creates values in, which is what makes
 * parse(print(parse(text))) structurally identical — the ids line up.
 *
 * The grammar (parse.c accepts precisely this; the two files are a pair):
 *
 *   module  := (sym | global | func)*
 *   sym     := "sym" symname                    ; full table, index order
 *   global  := "global" symname "size" INT "align" INT linkage
 *              ["tentative"] ["init" HEX*]
 *              ("reloc" INT symname INT)*
 *   func    := "func" type symname "(" [type %name ,...] ")" "{" block+ "}"
 *   block   := name "(" [type %name ,...] "):" inst*
 *   symname := @name | @!name                    ; ! = exact asm spelling
 *
 * Per-op instruction shapes are in print_inst below; operand atoms are
 * %value, signed decimal (iconst), 0xHEX[:0xHEX] (fconst exact bits,
 * hi:lo for f80/f128), @name[+addend], or undef. */

static const char *const type_names[] = {
    "i8",  "i16",   "i32",   "i64",   "f32",   "f64",   "f80",   "f128",
    "ptr", "v16i8", "v8i16", "v4i32", "v2i64", "v4f32", "v2f64", "void",
};

static const char *const etype_names[] = {
    "unknown", "char", "i8",   "i16", "i32",       "i64",   "f32",
    "f64",     "f80",  "f128", "ptr", "aggregate", "union",
};

static const char *const op_names[] = {
    "iadd",        "isub",        "imul",         "sdiv",       "udiv",
    "srem",        "urem",        "and",          "or",         "xor",
    "shl",         "lshr",        "ashr",         "icmp",       "fcmp",
    "fadd",        "fsub",        "fmul",         "fdiv",       "fneg",
    "sext",        "zext",        "trunc",        "fpext",      "fptrunc",
    "fptosi",      "fptoui",      "sitofp",       "uitofp",     "bitcast",
    "alloca",      "load",        "store",        "ptradd",     "memcpy",
    "memset",      "call",        "select",       "vsplat",     "vextract",
    "vreduce_add", "vreduce_mul", "vreduce_and",  "vreduce_or", "vreduce_xor",
    "va_start",    "stacksave",   "stackrestore", "atomicrmw",  "cmpxchg",
    "asm",         "ret",         "br",           "condbr",     "switch",
    "unreachable",
};

static const char *const rmw_names[] = {
    "add", "sub", "and", "or", "xor", "xchg",
};

const char *ir_rmw_name(u8 k)
{
    if (k > RMW_XCHG)
        CGF_ICE("ir printer: bad rmw op %u", k);
    return rmw_names[k];
}

/* IrAbiRet spellings for the func-header `abi(...)` marker and the pair
 * call-arg annotations; index = enum value. */
static const char *const abi_ret_names[] = {
    "none",    "sret",    "pair_ii", "pair_is",  "pair_si",
    "pair_ss", "hfa_f32", "hfa_f64", "hfa_f128",
};

const char *ir_abi_ret_name(u8 k)
{
    if (k > IR_ABIRET_HFA_F128)
        CGF_ICE("ir printer: bad abi_ret %u", k);
    return abi_ret_names[k];
}

static const char *const icmp_names[] = {
    "eq", "ne", "slt", "sle", "sgt", "sge", "ult", "ule", "ugt", "uge",
};

static const char *const fcmp_names[] = {
    "oeq", "one", "olt", "ole", "ogt", "oge", "ord",
    "ueq", "une", "ult", "ule", "ugt", "uge", "uno",
};

const char *ir_type_name(IrType t)
{
    if ((unsigned)t > IRT_VOID)
        CGF_ICE("ir printer: bad type %d", (int)t);
    return type_names[t];
}

const char *ir_etype_name(EffTypeId t)
{
    if ((unsigned)t >= ETYPE_COUNT)
        CGF_ICE("ir printer: bad effective type %d", (int)t);
    return etype_names[t];
}

const char *ir_op_name(IrOp op)
{
    if ((unsigned)op > IR_UNREACHABLE)
        CGF_ICE("ir printer: bad opcode %d", (int)op);
    return op_names[op];
}

const char *ir_icmp_name(IrIcmp p)
{
    if ((unsigned)p > ICMP_UGE)
        CGF_ICE("ir printer: bad icmp pred %d", (int)p);
    return icmp_names[p];
}

const char *ir_fcmp_name(IrFcmp p)
{
    if ((unsigned)p > FCMP_UNO)
        CGF_ICE("ir printer: bad fcmp pred %d", (int)p);
    return fcmp_names[p];
}

/* Value renumbering table: id -> printed number, built in document order. */
typedef struct {
    u32 *num; /* indexed by ValueId.v; u32-max = unnumbered */
    u32 nvals;
} ValNames;

#define VN_NONE 0xFFFFFFFFu

static ValNames vn_build(Arena *a, const IrFunc *f)
{
    ValNames vn;
    u32 next = 0;
    u32 i;
    const IrBlock *blk;
    const IrInst *in;

    vn.nvals = f->nvals;
    vn.num = arena_alloc(a, (f->nvals + 1) * sizeof(u32), _Alignof(u32));
    memset(vn.num, 0xFF, (f->nvals + 1) * sizeof(u32));
    for (i = 0; i < f->nparams; i++)
        vn.num[f->param_vals[i].v] = next++;
    for (i = 0; i < f->nblocks; i++) {
        u32 j;

        blk = &f->blocks[i];
        for (j = 0; j < blk->nparams; j++)
            vn.num[blk->params[j].v] = next++;
        for (in = blk->first; in; in = in->next)
            if (in->result.v)
                vn.num[in->result.v] = next++;
    }
    return vn;
}

/* An arbitrary byte string, QUOTED. A section name needs it because
 * `.note.GNU-stack` does not lex as an identifier -- printing it bare is what
 * left `section` unparseable and made -emit-ir ICE on any program using it.
 *
 * NEWLINE AND TAB ARE ESCAPED TOO, which the section case never needed and
 * the asm case cannot do without: an asm template is routinely multi-line
 * (`"movl %1, %0\n\taddl $1, %0"`), and a raw newline inside a quoted string
 * would end the instruction's LINE in a line-oriented format. The IR parser
 * decodes exactly these four. */
static void print_quoted(Buf *out, const char *s)
{
    buf_printf(out, "\"");
    for (; *s; s++) {
        switch (*s) {
        case '"':
            buf_printf(out, "\\\"");
            break;
        case '\\':
            buf_printf(out, "\\\\");
            break;
        case '\n':
            buf_printf(out, "\\n");
            break;
        case '\t':
            buf_printf(out, "\\t");
            break;
        default:
            buf_printf(out, "%c", *s);
            break;
        }
    }
    buf_printf(out, "\"");
}

static void print_val(Buf *out, const ValNames *vn, u32 id)
{
    if (id == 0 || id > vn->nvals || vn->num[id] == VN_NONE)
        buf_printf(out, "%%bad%u", id); /* dump-only; parser rejects */
    else
        buf_printf(out, "%%%u", vn->num[id]);
}

static void print_sym_name(Buf *out, const char *name)
{
    if (!name) {
        buf_printf(out, "@?");
        return;
    }
    buf_printf(out, ir_sym_name_is_exact_asm(name) ? "@!%s" : "@%s",
               ir_sym_asm_spelling(name));
}

/* An atom: the operand payload WITHOUT its type. */
static void print_atom(Buf *out, const IrModule *m, const ValNames *vn,
                       const IrOperand *o)
{
    switch (o->kind) {
    case IROP_VALUE:
        print_val(out, vn, (u32)o->a);
        break;
    case IROP_ICONST:
        buf_printf(out, "%lld", (long long)(i64)o->a);
        break;
    case IROP_FCONST:
        switch (o->type) {
        case IRT_F32:
            buf_printf(out, "0x%08llX",
                       (unsigned long long)(o->a & 0xFFFFFFFFu));
            break;
        case IRT_F64:
            buf_printf(out, "0x%016llX", (unsigned long long)o->a);
            break;
        case IRT_F80:
            buf_printf(out, "0x%04llX:0x%016llX", (unsigned long long)o->b,
                       (unsigned long long)o->a);
            break;
        default: /* f128 */
            buf_printf(out, "0x%016llX:0x%016llX", (unsigned long long)o->b,
                       (unsigned long long)o->a);
            break;
        }
        break;
    case IROP_SYMBOL:
        print_sym_name(out, o->sym < m->nsyms ? m->syms[o->sym] : NULL);
        if ((i64)o->a > 0)
            buf_printf(out, "+%lld", (long long)(i64)o->a);
        else if ((i64)o->a < 0)
            buf_printf(out, "%lld", (long long)(i64)o->a);
        break;
    case IROP_UNDEF:
        buf_printf(out, "undef");
        break;
    default:
        buf_printf(out, "?");
        break;
    }
}

/* A typed operand: "i32 %4", "ptr @g+8", "f64 0x...". */
static void print_typed(Buf *out, const IrModule *m, const ValNames *vn,
                        const IrOperand *o)
{
    buf_printf(out, "%s ", type_names[o->type]);
    print_atom(out, m, vn, o);
}

/* A call argument: typed operand plus its ABI annotation, if any. */
static void print_call_arg(Buf *out, const IrModule *m, const ValNames *vn,
                           const IrOperand *o)
{
    u32 kind =
        o->kind == IROP_VALUE || o->kind == IROP_SYMBOL ? ir_arg_kind(o->b) : 0;

    print_typed(out, m, vn, o);
    switch (kind) {
    case IR_ARG_BYVAL:
        buf_printf(out, " byval(%u)", ir_arg_size(o->b));
        break;
    case IR_ARG_HFA:
        buf_printf(out, " hfa(%u,%u)", ir_arg_size(o->b), ir_arg_hfa_n(o->b));
        break;
    case IR_ARG_SRET:
    case IR_ARG_PAIR_II:
    case IR_ARG_PAIR_IS:
    case IR_ARG_PAIR_SI:
    case IR_ARG_PAIR_SS:
        buf_printf(out, " %s(%u)", ir_abi_ret_name((u8)(kind - 1)),
                   ir_arg_size(o->b));
        break;
    default:
        break;
    }
    if (o->argflags & IROPF_ANON)
        buf_printf(out, " anon");
    if (o->argflags & IROPF_SEXT)
        buf_printf(out, " sext");
    if (o->argflags & IROPF_ZEXT)
        buf_printf(out, " zext");
    if (o->argflags & IROPF_ONSTACK)
        buf_printf(out, " onstack");
}

static void print_edge(Buf *out, const IrModule *m, const IrFunc *f,
                       const ValNames *vn, const IrEdge *e)
{
    const IrBlock *t = ir_block((IrFunc *)f, e->target);
    u32 i;

    buf_printf(out, "%s(", t && t->name ? t->name : "?");
    for (i = 0; i < e->nargs; i++) {
        if (i)
            buf_printf(out, ", ");
        print_typed(out, m, vn, &e->args[i]);
    }
    buf_printf(out, ")");
}

static void print_memflags(Buf *out, u8 flags)
{
    if (flags & IRF_VOLATILE)
        buf_printf(out, ", volatile");
    if (flags & IRF_SEQ_CST)
        buf_printf(out, ", seq_cst");
    if (flags & IRF_SELF_INIT)
        buf_printf(out, ", self_init");
}

static void print_etype(Buf *out, const IrInst *in)
{
    if (in->subop != ETYPE_UNKNOWN)
        buf_printf(out, ", etype %s", ir_etype_name((EffTypeId)in->subop));
}

static void print_inst(Buf *out, const IrModule *m, const IrFunc *f,
                       const ValNames *vn, const IrInst *in)
{
    u32 i;

    buf_printf(out, "    ");
    if (in->result.v) {
        print_val(out, vn, in->result.v);
        buf_printf(out, " = ");
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
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FDIV:
        /* %r = iadd i32 %a, %b — one type covers operands and result */
        buf_printf(out, "%s %s ", op_names[in->op], type_names[in->type]);
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[1]);
        if (in->flags & IRF_NSW)
            buf_printf(out, ", nsw");
        break;
    case IR_ICMP:
        buf_printf(out, "icmp %s %s ", icmp_names[in->subop],
                   type_names[in->ops[0].type]);
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[1]);
        if (in->flags & IRF_BOUNDS_CHECK)
            buf_printf(out, ", bounds");
        break;
    case IR_FCMP:
        buf_printf(out, "fcmp %s %s ", fcmp_names[in->subop],
                   type_names[in->ops[0].type]);
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[1]);
        break;
    case IR_FNEG:
        buf_printf(out, "fneg %s ", type_names[in->type]);
        print_atom(out, m, vn, &in->ops[0]);
        break;
    case IR_VSPLAT:
        buf_printf(out, "vsplat %s ", type_names[in->type]);
        print_typed(out, m, vn, &in->ops[0]);
        break;
    case IR_VEXTRACT:
        buf_printf(out, "vextract %s ", type_names[in->ops[0].type]);
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", %u", in->subop);
        break;
    case IR_VREDUCE_ADD:
    case IR_VREDUCE_MUL:
    case IR_VREDUCE_AND:
    case IR_VREDUCE_OR:
    case IR_VREDUCE_XOR:
        buf_printf(out, "%s %s ", op_names[in->op],
                   type_names[in->ops[0].type]);
        print_atom(out, m, vn, &in->ops[0]);
        break;
    case IR_SEXT:
    case IR_ZEXT:
    case IR_TRUNC:
    case IR_FPEXT:
    case IR_FPTRUNC:
    case IR_FPTOSI:
    case IR_FPTOUI:
    case IR_SITOFP:
    case IR_UITOFP:
    case IR_BITCAST:
        /* %r = sext i32 %a to i64 */
        buf_printf(out, "%s ", op_names[in->op]);
        print_typed(out, m, vn, &in->ops[0]);
        buf_printf(out, " to %s", type_names[in->type]);
        break;
    case IR_ALLOCA:
        buf_printf(out, "alloca ");
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", align %u", in->align);
        print_etype(out, in);
        break;
    case IR_LOAD:
        buf_printf(out, "load %s, ", type_names[in->type]);
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", align %u", in->align);
        print_memflags(out, in->flags);
        print_etype(out, in);
        break;
    case IR_STORE:
        buf_printf(out, "store ");
        print_typed(out, m, vn, &in->ops[0]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[1]);
        buf_printf(out, ", align %u", in->align);
        print_memflags(out, in->flags);
        print_etype(out, in);
        break;
    case IR_PTRADD:
        buf_printf(out, "ptradd ");
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[1]);
        break;
    case IR_MEMCPY:
    case IR_MEMSET:
        buf_printf(out, "%s ", op_names[in->op]);
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[1]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[2]);
        buf_printf(out, ", align %u", in->align);
        /* bytewise operations imply ETYPE_CHAR, so the spelling stays
         * compact while parse(print(M)) retains the metadata. */
        print_memflags(out, in->flags);
        break;
    case IR_SELECT:
        buf_printf(out, "select ");
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", %s ", type_names[in->type]);
        print_atom(out, m, vn, &in->ops[1]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[2]);
        break;
    case IR_CALL: {
        u32 first = 0;

        buf_printf(out, "call %s ", type_names[in->type]);
        if (in->subop == FUNCREF_INTERNAL)
            print_sym_name(
                out, in->callee < m->nfuncs ? m->funcs[in->callee].name : NULL);
        else if (in->subop == FUNCREF_EXTERNAL)
            print_sym_name(out,
                           in->callee < m->nsyms ? m->syms[in->callee] : NULL);
        else {
            print_atom(out, m, vn, &in->ops[0]);
            first = 1;
        }
        buf_printf(out, "(");
        for (i = first; i < in->nops; i++) {
            if (i > first)
                buf_printf(out, ", ");
            print_call_arg(out, m, vn, &in->ops[i]);
        }
        buf_printf(out, ")");
        if (in->flags & IRF_CALL_VARIADIC)
            buf_printf(out, " va");
        if (in->flags & IRF_NORETURN)
            buf_printf(out, " noreturn");
        break;
    }
    case IR_VA_START:
        buf_printf(out, "va_start ");
        print_atom(out, m, vn, &in->ops[0]);
        break;
    case IR_STACKSAVE:
        buf_printf(out, "stacksave");
        break;
    case IR_STACKRESTORE:
        buf_printf(out, "stackrestore ");
        print_atom(out, m, vn, &in->ops[0]);
        break;
    case IR_ASM: {
        /* The whole record prints inline rather than as a reference into a
         * module-level table, so one line of IR text is one asm and the
         * round-trip needs no side channel. */
        const IrAsm *a = in->callee && in->callee <= m->nasms
                             ? &m->asms[in->callee - 1]
                             : NULL;
        u32 i;

        buf_printf(out, "asm");
        if (a && a->is_volatile)
            buf_printf(out, " volatile");
        if (a && a->is_basic)
            buf_printf(out, " basic");
        buf_printf(out, " ");
        print_quoted(out, a ? a->tmpl : "");
        for (i = 0; a && i < a->nops; i++) {
            buf_printf(out, ", %s", a->ops[i].is_output ? "out" : "in");
            if (a->ops[i].early_clobber)
                buf_printf(out, "&");
            if (a->ops[i].tied_to >= 0)
                buf_printf(out, "=%d", a->ops[i].tied_to);
            buf_printf(out, " ");
            print_quoted(out, a->ops[i].constraint);
            buf_printf(out, " ");
            /* TYPED, because asm operands are not all one type: an output is
             * an ADDRESS while an input is a value, and constant folding can
             * turn an input into an iconst of its own width. The first draft
             * printed bare atoms and the parser assumed ptr for all of them,
             * so an optimized `"+r"` broke the round-trip self-check. */
            print_typed(out, m, vn, &in->ops[i]);
        }
        if (a && (a->clobbers_memory || a->clobbers_cc || a->nclobber_regs)) {
            buf_printf(out, ", clobbers");
            if (a->clobbers_memory)
                buf_printf(out, " memory");
            if (a->clobbers_cc)
                buf_printf(out, " cc");
            for (i = 0; i < a->nclobber_regs; i++)
                buf_printf(out, " r%u", (unsigned)a->clobber_regs[i]);
        }
        break;
    }
    case IR_ATOMICRMW:
        buf_printf(out, "atomicrmw %s %s ", rmw_names[in->subop],
                   type_names[in->type]);
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[1]);
        print_memflags(out, in->flags);
        break;
    case IR_CMPXCHG:
        buf_printf(out, "cmpxchg %s ", type_names[in->type]);
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[1]);
        buf_printf(out, ", ");
        print_atom(out, m, vn, &in->ops[2]);
        print_memflags(out, in->flags);
        break;
    case IR_RET:
        buf_printf(out, "ret");
        if (in->nops) {
            buf_printf(out, " ");
            print_typed(out, m, vn, &in->ops[0]);
        }
        if (in->flags & IRF_FLOW_PROVENANCE)
            buf_printf(out, ", implicit");
        break;
    case IR_BR:
        buf_printf(out, "br ");
        print_edge(out, m, f, vn, &in->edges[0]);
        if (in->flags & IRF_FLOW_PROVENANCE)
            buf_printf(out, ", defensive");
        break;
    case IR_CONDBR:
        buf_printf(out, "condbr ");
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", ");
        print_edge(out, m, f, vn, &in->edges[0]);
        buf_printf(out, ", ");
        print_edge(out, m, f, vn, &in->edges[1]);
        if (in->flags & IRF_FLOW_PROVENANCE)
            buf_printf(out, ", config");
        break;
    case IR_SWITCH:
        buf_printf(out, "switch %s ", type_names[in->ops[0].type]);
        print_atom(out, m, vn, &in->ops[0]);
        buf_printf(out, ", ");
        print_edge(out, m, f, vn, &in->edges[0]);
        for (i = 1; i < in->nedges; i++) {
            buf_printf(out, ", %lld: ", (long long)in->edges[i].case_val);
            print_edge(out, m, f, vn, &in->edges[i]);
        }
        if (in->flags & IRF_FLOW_PROVENANCE)
            buf_printf(out, ", config");
        break;
    case IR_UNREACHABLE:
        buf_printf(out, "unreachable");
        break;
    default:
        /* Reserved opcodes never reach the printer (builder ICEs), but a
         * corrupt module dumped via CGF_DUMP_BAD_IR might. */
        buf_printf(out, "<op %u>", in->op);
        break;
    }
    buf_printf(out, "\n");
}

static void print_func(Buf *out, const IrModule *m, const IrFunc *f)
{
    Arena scratch;
    ValNames vn;
    u32 i;

    arena_init(&scratch);
    vn = vn_build(&scratch, f);

    buf_printf(out, "func %s ", type_names[f->ret]);
    print_sym_name(out, f->name);
    buf_printf(out, "(");
    for (i = 0; i < f->nparams; i++) {
        if (i)
            buf_printf(out, ", ");
        buf_printf(out, "%s ", type_names[f->param_types[i]]);
        if (f->param_annots && ir_arg_kind(f->param_annots[i]) == IR_ARG_BYVAL)
            buf_printf(out, "byval(%u) ", ir_arg_size(f->param_annots[i]));
        if (f->param_annots && ir_param_is_onstack(f->param_annots[i]))
            buf_printf(out, "onstack ");
        if (f->param_annots && ir_param_is_restrict(f->param_annots[i]))
            buf_printf(out, "restrict ");
        print_val(out, &vn, f->param_vals[i].v);
    }
    if (f->variadic)
        buf_printf(out, "%s...", f->nparams ? ", " : "");
    buf_printf(out, ")");
    if (f->unprototyped)
        buf_printf(out, " unproto");
    if (f->linkage == IRLINK_INTERNAL)
        buf_printf(out, " internal");
    if (f->is_weak)
        buf_printf(out, " weak");
    if (f->visibility)
        buf_printf(out, " visibility(%s)", gnu_visibility_name(f->visibility));
    if (f->align)
        buf_printf(out, " align(%u)", f->align);
    if (f->is_used)
        buf_printf(out, " used");
    if (f->section) {
        buf_printf(out, " section(");
        print_quoted(out, f->section);
        buf_printf(out, ")");
    }
    /* The priority is printed only when it is not the default, mirroring what
     * the attribute itself means and keeping every existing golden unchanged.
     */
    if (f->is_ctor) {
        if (f->ctor_prio == CGF_INIT_PRIORITY_DEFAULT)
            buf_printf(out, " constructor");
        else
            buf_printf(out, " constructor(%u)", f->ctor_prio);
    }
    if (f->is_dtor) {
        if (f->dtor_prio == CGF_INIT_PRIORITY_DEFAULT)
            buf_printf(out, " destructor");
        else
            buf_printf(out, " destructor(%u)", f->dtor_prio);
    }
    if (f->abi_ret >= IR_ABIRET_HFA_F32)
        buf_printf(out, " abi(%s,%u)", ir_abi_ret_name(f->abi_ret),
                   f->abi_ret_n);
    else if (f->abi_ret != IR_ABIRET_NONE)
        buf_printf(out, " abi(%s)", ir_abi_ret_name(f->abi_ret));
    if (f->calls_setjmp)
        buf_printf(out, " setjmp");
    if (f->fp_contract)
        buf_printf(out, " contract");
    buf_printf(out, " {\n");
    for (i = 0; i < f->nblocks; i++) {
        const IrBlock *blk = &f->blocks[i];
        const IrInst *in;
        u32 j;

        buf_printf(out, "%s(", blk->name ? blk->name : "?");
        for (j = 0; j < blk->nparams; j++) {
            if (j)
                buf_printf(out, ", ");
            buf_printf(out, "%s ",
                       type_names[f->vals[blk->params[j].v - 1].type]);
            print_val(out, &vn, blk->params[j].v);
        }
        buf_printf(out, "):\n");
        for (in = blk->first; in; in = in->next)
            print_inst(out, m, f, &vn, in);
    }
    buf_printf(out, "}\n");
    arena_free_all(&scratch);
}

void ir_print_module_buf(Buf *out, const IrModule *m)
{
    u32 i, j;

    for (i = 0; i < m->nsyms; i++) {
        buf_printf(out, "sym ");
        print_sym_name(out, m->syms[i]);
        if (m->sym_attrs[i].is_weak)
            buf_printf(out, " weak");
        if (m->sym_attrs[i].visibility)
            buf_printf(out, " visibility(%s)",
                       gnu_visibility_name(m->sym_attrs[i].visibility));
        buf_printf(out, "\n");
    }
    /* Aliases before globals: an alias names a target defined LATER in the
     * file as often as earlier, and the parser resolves targets by name at the
     * end anyway, so the order is a readability choice rather than a
     * constraint. Keeping them first groups the whole symbol surface. */
    for (i = 0; i < m->naliases; i++) {
        const IrAlias *a = &m->aliases[i];
        static const char *const alink[] = {"internal", "external", "common"};

        buf_printf(out, "alias ");
        print_sym_name(out, a->name);
        buf_printf(out, " = ");
        print_sym_name(out, a->target);
        buf_printf(out, " %s", alink[a->linkage]);
        if (a->is_weak)
            buf_printf(out, " weak");
        if (a->visibility)
            buf_printf(out, " visibility(%s)",
                       gnu_visibility_name(a->visibility));
        buf_printf(out, "\n");
    }
    for (i = 0; i < m->nglobals; i++) {
        const IrGlobal *g = &m->globals[i];
        static const char *const link_names[] = {"internal", "external",
                                                 "common"};

        buf_printf(out, "global ");
        print_sym_name(out, g->name);
        buf_printf(out, " size %llu align %u %s", (unsigned long long)g->size,
                   g->align, link_names[g->linkage]);
        if (g->is_tentative)
            buf_printf(out, " tentative");
        if (g->is_tls)
            buf_printf(out, " tls");
        if (g->is_weak)
            buf_printf(out, " weak");
        if (g->is_used)
            buf_printf(out, " used");
        if (g->is_const)
            buf_printf(out, " const");
        if (g->section) {
            buf_printf(out, " section(");
            print_quoted(out, g->section);
            buf_printf(out, ")");
        }
        if (g->visibility)
            buf_printf(out, " visibility(%s)",
                       gnu_visibility_name(g->visibility));
        if (g->init) {
            /* x-prefixed so a digit-leading image lexes as one ident */
            buf_printf(out, " init x");
            for (j = 0; j < g->size; j++)
                buf_printf(out, "%02x", g->init[j]);
        }
        for (j = 0; j < g->nrelocs; j++) {
            buf_printf(out, " reloc %llu ",
                       (unsigned long long)g->relocs[j].offset);
            print_sym_name(out, m->syms[g->relocs[j].symbol]);
            buf_printf(out, " %lld", (long long)g->relocs[j].addend);
        }
        buf_printf(out, "\n");
    }
    for (i = 0; i < m->nfuncs; i++) {
        buf_printf(out, "\n");
        print_func(out, m, &m->funcs[i]);
    }
}

void ir_print_module(FILE *out, const IrModule *m)
{
    Buf b;

    buf_init(&b);
    ir_print_module_buf(&b, m);
    fwrite(b.data, 1, b.len, out);
    buf_free(&b);
}
