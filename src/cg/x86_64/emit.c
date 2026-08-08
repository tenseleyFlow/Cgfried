#include "cg/shared.h"
#include "cg/x86_64/mir.h"

#include <string.h>

/* AT&T emission (Sprint 24): post-RA MIR -> gas-assemblable `.s`.
 *
 * THE RULES, stated once and applied everywhere:
 *   - Operand order is src, dst. The operand-print helpers take (src,
 *     dst) parameters BY NAME so a transposition is visible at the
 *     call site.
 *   - Suffixes are chosen by width, never omitted: movq/movl/movw/movb.
 *     afs-as tolerates bare forms in places; we emit only what gas and
 *     afs-as agree on.
 *   - RIP-relative for ALL global data addresses (one form, PIC-ready;
 *     afs-as rejects symbolic immediates by design, so `$sym` is not
 *     even assemblable).
 *   - `.p2align` only (`.align` is arch-dependent; `.balign` is not in
 *     afs-as — deliberately skipped upstream).
 *   - Data is emitted NUMERICALLY (.byte 104, ...): no .ascii/.asciz,
 *     which sidesteps assembler escape-dialect subtleties entirely.
 *   - Local labels are .Lf<func-idx>_<n> — deterministic across runs.
 *   - Branches are always symbolic; the assembler's relaxation pass
 *     sizes them (rel8/rel32). No compiler-side branch sizing, ever.
 *
 * x87 mnemonic table carries F-S23-ATTX87SWAP: gas assembles the
 * subtract/divide POP forms swapped (`fsubp` -> Intel FSUBRP bytes),
 * so the ops meaning `st1 := st1 OP st0, pop` are SPELLED fsubrp /
 * fdivrp here. The afs-as differential suite pins both assemblers. */

/* --- register names, per width ---------------------------------------------
 */

static const char *const rq[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp",
                                   "rsi", "rdi", "r8",  "r9",  "r10", "r11",
                                   "r12", "r13", "r14", "r15"};
static const char *const rl[16] = {
    "eax", "ecx", "edx",  "ebx",  "esp",  "ebp",  "esi",  "edi",
    "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
static const char *const rw[16] = {
    "ax",  "cx",  "dx",   "bx",   "sp",   "bp",   "si",   "di",
    "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"};
static const char *const rb[16] = {
    "al",  "cl",  "dl",   "bl",   "spl",  "bpl",  "sil",  "dil",
    "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"};

/* Physical id (X64Reg + 1 encoding) -> name at the given width. */
static const char *regn(u32 id, u8 w)
{
    u32 r = id - 1;

    if (r >= X64_XMM0) {
        static const char *const rx[16] = {"xmm0",  "xmm1",  "xmm2",  "xmm3",
                                           "xmm4",  "xmm5",  "xmm6",  "xmm7",
                                           "xmm8",  "xmm9",  "xmm10", "xmm11",
                                           "xmm12", "xmm13", "xmm14", "xmm15"};

        return rx[r - X64_XMM0];
    }
    switch (w) {
    case X64_B:
        return rb[r];
    case X64_W:
        return rw[r];
    case X64_L:
        return rl[r];
    default:
        return rq[r];
    }
}

static char sfx(u8 w)
{
    return w == X64_B ? 'b' : w == X64_W ? 'w' : w == X64_L ? 'l' : 'q';
}

/* --- emission context -------------------------------------------------------
 */

typedef struct Emit {
    Buf *out;
    const X64Func *f;
    const IrModule *m;
    u32 fidx; /* function index: label namespace .Lf<fidx>_<n> */
} Emit;

static void pmem_att(Emit *e, const X64Mem *m)
{
    if (m->seg_fs) {
        /* The thread pointer itself. %fs:0 holds the TCB address on x86-64
         * Linux, and every thread-local is at a fixed offset from it. */
        buf_printf(e->out, "%%fs:%d", m->disp);
        return;
    }
    if (m->tpoff_sym) {
        /* `sym@tpoff(%base)`: the link-time offset of a thread-local from
         * the thread pointer, added to the thread pointer in %base.
         * R_X86_64_TPOFF32, resolved by the linker because only it knows the
         * final layout of the initial TLS block. */
        buf_printf(e->out, "%s@tpoff", e->m->syms[m->tpoff_sym - 1]);
        if (m->disp)
            buf_printf(e->out, "%+d", m->disp);
        if (m->base.v)
            buf_printf(e->out, "(%%%s)", regn(m->base.v, X64_Q));
        return;
    }
    if (m->rip_sym) {
        buf_printf(e->out, "%s", e->m->syms[m->rip_sym - 1]);
        if (m->disp)
            buf_printf(e->out, "%+d", m->disp);
        /* @GOTPCREL names the symbol's GOT SLOT. Plain GOTPCREL rather than
         * GOTPCRELX: a relaxation-capable linker turns the load back into an
         * lea where it can prove the symbol is local, and choosing the
         * relaxable spelling ourselves would only duplicate that judgement
         * with less information. isel guarantees no addend rides here. */
        buf_printf(e->out, "%s(%%rip)", m->rip_got ? "@GOTPCREL" : "");
        return;
    }
    if (m->cpool) {
        buf_printf(e->out, ".LCf%u_%u(%%rip)", e->fidx, m->cpool - 1);
        return;
    }
    if (m->disp)
        buf_printf(e->out, "%d", m->disp);
    buf_printf(e->out, "(");
    if (m->base.v)
        buf_printf(e->out, "%%%s", regn(m->base.v, X64_Q));
    if (m->index.v)
        buf_printf(e->out, ",%%%s,%u", regn(m->index.v, X64_Q), m->scale);
    buf_printf(e->out, ")");
}

/* One operand at a width. */
static void poper_att(Emit *e, const X64Operand *o, u8 w)
{
    switch (o->kind) {
    case X64O_VREG:
        buf_printf(e->out, "%%%s", regn(o->r.v, w));
        break;
    case X64O_IMM:
        buf_printf(e->out, "$%lld", (long long)o->imm);
        break;
    case X64O_MEM:
        pmem_att(e, &o->mem);
        break;
    default:
        CGF_ICE("x64 emit: empty operand printed");
    }
}

/* `op src, dst` — THE AT&T order, named so transpositions are visible. */
static void emit2(Emit *e, const char *op, const X64Operand *src, u8 srcw,
                  const X64Operand *dst, u8 dstw)
{
    buf_printf(e->out, "\t%s\t", op);
    poper_att(e, src, srcw);
    buf_printf(e->out, ", ");
    poper_att(e, dst, dstw);
    buf_printf(e->out, "\n");
}

static void emit1(Emit *e, const char *op, const X64Operand *o, u8 w)
{
    buf_printf(e->out, "\t%s\t", op);
    poper_att(e, o, w);
    buf_printf(e->out, "\n");
}

static void emit0(Emit *e, const char *op)
{
    buf_printf(e->out, "\t%s\n", op);
}

static X64Operand defop(const X64Inst *in)
{
    X64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = X64O_VREG;
    o.r = in->def;
    return o;
}

/* --- the instruction table --------------------------------------------------
 */

static void emit_inst(Emit *e, const X64Inst *in, u32 bi, u32 next_bb)
{
    char op[24];
    X64Operand d = defop(in);
    u8 w = in->width;

    switch (in->op) {
    case X64_OP_MOV:
        snprintf(op, sizeof(op), "mov%c", sfx(w));
        emit2(e, op, &in->a, w, &d, w);
        break;
    case X64_OP_MOVABS:
        emit2(e, "movabsq", &in->a, X64_Q, &d, X64_Q);
        break;
    case X64_OP_MOVZX:
    case X64_OP_MOVSX: {
        /* movz<s><d> / movs<s><d>; the l->q zext is mov's job and isel
         * never emits it. movslq is the one irregular spelling. */
        bool sx = in->op == X64_OP_MOVSX;

        if (sx && in->src_width == X64_L && w == X64_Q)
            snprintf(op, sizeof(op), "movslq");
        else
            snprintf(op, sizeof(op), "mov%c%c%c", sx ? 's' : 'z',
                     sfx(in->src_width), sfx(w));
        emit2(e, op, &in->a, in->src_width, &d, w);
        break;
    }
    case X64_OP_LEA:
        snprintf(op, sizeof(op), "lea%c", sfx(w));
        emit2(e, op, &in->a, w, &d, w);
        break;
    case X64_OP_ADD:
    case X64_OP_SUB:
    case X64_OP_AND:
    case X64_OP_OR:
    case X64_OP_XOR:
    case X64_OP_IMUL: {
        static const char *const stems[] = {"add", "sub", "and",
                                            "or",  "xor", "imul"};

        snprintf(op, sizeof(op), "%s%c", stems[in->op - X64_OP_ADD], sfx(w));
        if (in->op == X64_OP_IMUL && in->b.kind == X64O_IMM) {
            /* x86 has no two-operand imul-with-immediate: the AT&T
             * 3-operand form `imul $imm, src, dst` (src == dst after
             * the two-address fixup) is the real instruction. */
            buf_printf(e->out, "\t%s\t", op);
            poper_att(e, &in->b, w);
            buf_printf(e->out, ", ");
            poper_att(e, &d, w);
            buf_printf(e->out, ", ");
            poper_att(e, &d, w);
            buf_printf(e->out, "\n");
            break;
        }
        /* post-fixup canonical def==a: text is `op b, dst` */
        emit2(e, op, &in->b, w, &d, w);
        break;
    }
    case X64_OP_NEG:
    case X64_OP_NOT:
        snprintf(op, sizeof(op), "%s%c", in->op == X64_OP_NEG ? "neg" : "not",
                 sfx(w));
        emit1(e, op, &d, w);
        break;
    case X64_OP_SHL:
    case X64_OP_SHR:
    case X64_OP_SAR: {
        static const char *const stems[] = {"shl", "shr", "sar"};

        snprintf(op, sizeof(op), "%s%c", stems[in->op - X64_OP_SHL], sfx(w));
        if (in->b.kind == X64O_VREG) {
            /* variable count: always %cl */
            buf_printf(e->out, "\t%s\t%%cl, ", op);
            poper_att(e, &d, w);
            buf_printf(e->out, "\n");
        } else {
            emit2(e, op, &in->b, w, &d, w);
        }
        break;
    }
    case X64_OP_CMP:
    case X64_OP_TEST:
        /* MIR flags(a ? b) = AT&T `cmp b, a` (dst - src). */
        snprintf(op, sizeof(op), "%s%c", in->op == X64_OP_CMP ? "cmp" : "test",
                 sfx(w));
        emit2(e, op, &in->b, w, &in->a, w);
        break;
    case X64_OP_SETCC:
        snprintf(op, sizeof(op), "set%s", x64_cc_name(in->cc));
        emit1(e, op, &d, X64_B);
        break;
    case X64_OP_CQO:
        emit0(e, w == X64_L ? "cltd" : "cqto");
        break;
    case X64_OP_IDIV:
    case X64_OP_DIV:
        snprintf(op, sizeof(op), "%s%c", in->op == X64_OP_IDIV ? "idiv" : "div",
                 sfx(w));
        emit1(e, op, &in->b, w);
        break;
    case X64_OP_LOAD:
        snprintf(op, sizeof(op), "mov%c", sfx(w));
        emit2(e, op, &in->a, w, &d, w);
        break;
    case X64_OP_STORE:
        snprintf(op, sizeof(op), "mov%c", sfx(w));
        emit2(e, op, &in->a, w, &in->b, w);
        break;
    case X64_OP_XADD:
    case X64_OP_CMPXCHG:
    case X64_OP_XCHG:
        /* AT&T order is (src, dst): the register is the source and memory
         * the destination for all three. XCHG against memory is atomic with
         * no prefix, which is why only the other two carry X64IF_LOCK. */
        snprintf(op, sizeof(op), "%s%s%c",
                 in->flags & X64IF_LOCK ? "lock " : "",
                 in->op == X64_OP_XADD      ? "xadd"
                 : in->op == X64_OP_CMPXCHG ? "cmpxchg"
                                            : "xchg",
                 sfx(w));
        emit2(e, op, &in->a, w, &in->b, w);
        break;
    case X64_OP_MFENCE:
        buf_printf(e->out, "\tmfence\n");
        break;
    case X64_OP_JMP:
        buf_printf(e->out, "\tjmp\t.Lf%u_%u\n", e->fidx, in->target);
        break;
    case X64_OP_JCC:
        buf_printf(e->out, "\tj%s\t.Lf%u_%u\n", x64_cc_name(in->cc), e->fidx,
                   in->target);
        if (in->target2 && in->target2 != next_bb)
            buf_printf(e->out, "\tjmp\t.Lf%u_%u\n", e->fidx, in->target2);
        break;
    case X64_OP_JMPTBL:
        /* RIP-relative table base through r10 (reserved; the index
         * reload scratch is r11, so they cannot collide), then the
         * indirect jump. Sidesteps absolute symbolic displacements. */
        buf_printf(e->out, "\tleaq\t.Lf%utbl%u(%%rip), %%r10\n", e->fidx,
                   in->table);
        buf_printf(e->out, "\tjmp\t*(%%r10,%%%s,8)\n", regn(in->a.r.v, X64_Q));
        break;
    case X64_OP_RET:
        emit0(e, "ret");
        break;
    case X64_OP_UD2:
        emit0(e, "ud2");
        break;
    case X64_OP_PUSH:
        emit1(e, "pushq", &in->a, X64_Q);
        break;
    case X64_OP_POP:
        emit1(e, "popq", &d, X64_Q);
        break;
    case X64_OP_CALL:
        if (in->a.kind == X64O_VREG) {
            buf_printf(e->out, "\tcall\t*%%%s\n", regn(in->a.r.v, X64_Q));
        } else if (in->a.kind == X64O_MEM && in->a.mem.rip_sym) {
            buf_printf(e->out, "\tcall\t%s%s\n",
                       e->m->syms[in->a.mem.rip_sym - 1],
                       (in->flags & X64IF_CALL_PLT) ? "@PLT" : "");
        } else if (in->table) {
            buf_printf(e->out, "\tcall\t%s%s\n",
                       e->m->funcs[in->table - 1].name,
                       (in->flags & X64IF_CALL_PLT) ? "@PLT" : "");
        } else {
            CGF_ICE("x64 emit: call without a target");
        }
        break;
    /* --- SSE scalar --------------------------------------------------- */
    case X64_OP_FMOV:
    case X64_OP_FLOAD:
        emit2(e, w == X64_L ? "movss" : "movsd", &in->a, w, &d, w);
        break;
    case X64_OP_FSTORE:
        emit2(e, w == X64_L ? "movss" : "movsd", &in->a, w, &in->b, w);
        break;
    case X64_OP_FADD:
    case X64_OP_FSUB:
    case X64_OP_FMUL:
    case X64_OP_FDIV: {
        static const char *const stems[] = {"add", "sub", "mul", "div"};

        snprintf(op, sizeof(op), "%ss%c", stems[in->op - X64_OP_FADD],
                 w == X64_L ? 's' : 'd');
        emit2(e, op, &in->b, w, &d, w);
        break;
    }
    case X64_OP_UCOMI:
        emit2(e, w == X64_L ? "ucomiss" : "ucomisd", &in->b, w, &in->a, w);
        break;
    case X64_OP_CVTF2I:
        /* the TRUNCATING form; suffix is the INT width */
        snprintf(op, sizeof(op), "cvtts%c2si%c",
                 in->src_width == X64_L ? 's' : 'd', sfx(w));
        emit2(e, op, &in->a, in->src_width, &d, w);
        break;
    case X64_OP_CVTI2F:
        snprintf(op, sizeof(op), "cvtsi2s%c%c", w == X64_L ? 's' : 'd',
                 sfx(in->src_width));
        emit2(e, op, &in->a, in->src_width, &d, w);
        break;
    case X64_OP_CVTF2F:
        emit2(e, w == X64_Q ? "cvtss2sd" : "cvtsd2ss", &in->a, in->src_width,
              &d, w);
        break;
    case X64_OP_FXORM:
    case X64_OP_FANDM:
        snprintf(op, sizeof(op), "%sp%c",
                 in->op == X64_OP_FXORM ? "xor" : "and",
                 w == X64_L ? 's' : 'd');
        emit2(e, op, &in->b, w, &d, w);
        break;
    case X64_OP_MOVQXR:
    case X64_OP_MOVQRX:
        /* GP<->xmm bit bridges: movq (64) / movd (32). */
        emit2(e, w == X64_L ? "movd" : "movq", &in->a, w, &d, w);
        break;
    /* --- SSE2 packed vectors ---------------------------------------- */
    case X64_OP_VMOV:
    case X64_OP_VLOAD:
        emit2(e, "movdqu", &in->a, X64_X, &d, X64_X);
        break;
    case X64_OP_VSTORE:
        emit2(e, "movdqu", &in->a, X64_X, &in->b, X64_X);
        break;
    case X64_OP_VADD:
    case X64_OP_VSUB: {
        static const char *const addi[] = {"paddb", "paddw", "paddd", "paddq"};
        static const char *const subi[] = {"psubb", "psubw", "psubd", "psubq"};
        const char *mn;

        if (in->src_width >= IRT_V16I8 && in->src_width <= IRT_V2I64)
            mn = (in->op == X64_OP_VADD ? addi
                                        : subi)[in->src_width - IRT_V16I8];
        else if (in->src_width == IRT_V4F32)
            mn = in->op == X64_OP_VADD ? "addps" : "subps";
        else
            mn = in->op == X64_OP_VADD ? "addpd" : "subpd";
        emit2(e, mn, &in->b, X64_X, &d, X64_X);
        break;
    }
    case X64_OP_VMUL:
        emit2(e,
              in->src_width == IRT_V8I16   ? "pmullw"
              : in->src_width == IRT_V4F32 ? "mulps"
                                           : "mulpd",
              &in->b, X64_X, &d, X64_X);
        break;
    case X64_OP_VDIV:
        emit2(e, in->src_width == IRT_V4F32 ? "divps" : "divpd", &in->b, X64_X,
              &d, X64_X);
        break;
    case X64_OP_VAND:
    case X64_OP_VOR:
    case X64_OP_VXOR:
        emit2(e,
              in->op == X64_OP_VAND  ? "pand"
              : in->op == X64_OP_VOR ? "por"
                                     : "pxor",
              &in->b, X64_X, &d, X64_X);
        break;
    case X64_OP_VSHUF32:
        buf_printf(e->out, "\tpshufd\t$%lld, ", (long long)in->b.imm);
        poper_att(e, &in->a, X64_X);
        buf_printf(e->out, ", ");
        poper_att(e, &d, X64_X);
        buf_printf(e->out, "\n");
        break;
    case X64_OP_VSHUFLO16:
        buf_printf(e->out, "\tpshuflw\t$%lld, ", (long long)in->b.imm);
        poper_att(e, &in->a, X64_X);
        buf_printf(e->out, ", ");
        poper_att(e, &d, X64_X);
        buf_printf(e->out, "\n");
        break;
    case X64_OP_VUNPCKLBW:
        emit2(e, "punpcklbw", &in->b, X64_X, &d, X64_X);
        break;
    case X64_OP_VUNPCKLWD:
        emit2(e, "punpcklwd", &in->b, X64_X, &d, X64_X);
        break;
    case X64_OP_VUNPCKLQ:
        emit2(e, "punpcklqdq", &in->b, X64_X, &d, X64_X);
        break;
    case X64_OP_VSRLDQ:
        emit2(e, "psrldq", &in->b, X64_B, &d, X64_X);
        break;
    /* --- x87 (F-S23-ATTX87SWAP applied) -------------------------------- */
    case X64_OP_X87_FLD:
        snprintf(op, sizeof(op), "fld%c",
                 w == X64_T   ? 't'
                 : w == X64_Q ? 'l'
                              : 's');
        emit1(e, op, &in->a, w);
        break;
    case X64_OP_X87_FSTP:
        snprintf(op, sizeof(op), "fstp%c",
                 w == X64_T   ? 't'
                 : w == X64_Q ? 'l'
                              : 's');
        emit1(e, op, &in->a, w);
        break;
    case X64_OP_X87_FILD:
        emit1(e, w == X64_Q ? "fildq" : "fildl", &in->a, w);
        break;
    case X64_OP_X87_FISTP:
        emit1(e, w == X64_Q ? "fistpq" : "fistpl", &in->a, w);
        break;
    case X64_OP_X87_FADDP:
        emit0(e, "faddp");
        break;
    case X64_OP_X87_FSUBP:
        /* st1 := st1 - st0, pop = gas spelling fsubRp (the swap) */
        emit0(e, "fsubrp");
        break;
    case X64_OP_X87_FSUBRP:
        emit0(e, "fsubp");
        break;
    case X64_OP_X87_FMULP:
        emit0(e, "fmulp");
        break;
    case X64_OP_X87_FDIVP:
        emit0(e, "fdivrp");
        break;
    case X64_OP_X87_FDIVRP:
        emit0(e, "fdivp");
        break;
    case X64_OP_X87_FCHS:
        emit0(e, "fchs");
        break;
    case X64_OP_X87_FABS:
        emit0(e, "fabs");
        break;
    case X64_OP_X87_FUCOMIP:
        emit0(e, "fucomip");
        break;
    case X64_OP_X87_FPOP:
        emit0(e, "fstp\t%st(0)");
        break;
    case X64_OP_X87_FNSTCW:
        emit1(e, "fnstcw", &in->a, X64_W);
        break;
    case X64_OP_X87_FLDCW:
        emit1(e, "fldcw", &in->a, X64_W);
        break;
    default:
        CGF_ICE("x64 emit: unhandled op %u in bb%u", in->op, bi + 1);
    }
}

/* --- function emission ------------------------------------------------------
 */

/* GNU symbol attributes, emitted next to the binding directive they modify.
 *
 * `.weak` REPLACES `.globl` rather than joining it: gas takes whichever came
 * last, so emitting both happens to work in one order and silently does not
 * in the other. Emitting exactly one removes the question.
 *
 * Visibility is independent of binding and stacks on top. `default` emits
 * nothing -- it IS the default, and saying so explicitly only matters
 * against a -fvisibility= command line, which this compiler does not have. */
static void emit_symbol_attrs(Buf *out, const char *name, u8 visibility)
{
    if (visibility && visibility != GNU_VIS_DEFAULT)
        buf_printf(out, "\t.%s\t%s\n", gnu_visibility_name(visibility), name);
}

void x64_emit_function(const X64Func *f, const IrModule *m, u32 fidx,
                       u8 linkage, Buf *out)
{
    Emit e;
    u32 bi, i;

    if (!f->allocated)
        CGF_ICE("x64 emit: '%s' reached emission unallocated", f->name);
    e.out = out;
    e.f = f;
    e.m = m;
    e.fidx = fidx;

    {
        const IrFunc *sf = fidx < m->nfuncs ? &m->funcs[fidx] : NULL;

        if (sf && sf->section)
            buf_printf(out, "\t.section\t%s,\"ax\",@progbits\n", sf->section);
        else
            buf_printf(out, "\t.text\n");
    }
    /* The GNU attributes live on the IrFunc, not the MIR one: they are
     * properties of the SYMBOL, and MIR is about instructions. fidx is the
     * module index the driver already passes alongside the linkage. */
    {
        const IrFunc *irf = fidx < m->nfuncs ? &m->funcs[fidx] : NULL;
        bool weak = irf && irf->is_weak;

        if (linkage != IRLINK_INTERNAL)
            buf_printf(out, weak ? "\t.weak\t%s\n" : "\t.globl\t%s\n", f->name);
        else
            buf_printf(out, "\t.local\t%s\n", f->name);
        emit_symbol_attrs(out, f->name, irf ? irf->visibility : 0);
        buf_printf(out, "\t.p2align\t%u\n", cg_func_p2align(irf, 4));
    }
    buf_printf(out, "\t.type\t%s, @function\n", f->name);
    buf_printf(out, "%s:\n", f->name);
    /* A LOCAL alias for the entry, used by the .eh_frame FDE. Naming the
     * global symbol there emits a PC32 relocation against an interposable
     * symbol, and ld refuses that outright when making a shared object
     * ("can not be used when making a shared object; recompile with
     * -fPIC") -- so no object we produced could go into a .so, on any
     * target, and nothing noticed because we had never built one. gcc emits
     * the same local-label indirection for the same reason. */
    buf_printf(out, ".Lfb%u:\n", fidx);
    if (f->debug_lines)
        buf_printf(out, ".Lloc_%u_0:\n", fidx);
    for (bi = 0; bi < f->nblocks; bi++) {
        const X64Block *b = &f->blocks[bi];

        if (bi)
            buf_printf(out, ".Lf%u_%u:\n", fidx, bi + 1);
        for (i = 0; i < b->n; i++) {
            if (f->debug_lines && b->insts[i].debug_label)
                buf_printf(out, ".Lloc_%u_%u:\n", fidx,
                           b->insts[i].debug_label);
            emit_inst(&e, &b->insts[i], bi, bi + 2);
        }
    }
    buf_printf(out, ".Lfe%u_0:\n\t.size\t%s, .-%s\n", fidx, f->name, f->name);

    /* Per-function .rodata: jump tables (8-aligned) and the constant
     * pool (each entry at its own alignment), all fidx-namespaced. */
    if (f->ntables || f->nconsts)
        buf_printf(out, "\t.section\t.rodata\n");
    for (bi = 0; bi < f->ntables; bi++) {
        buf_printf(out, "\t.p2align\t3\n.Lf%utbl%u:\n", fidx, bi);
        for (i = 0; i < f->tables[bi].n; i++)
            buf_printf(out, "\t.quad\t.Lf%u_%u\n", fidx,
                       f->tables[bi].targets[i]);
    }
    for (bi = 0; bi < f->nconsts; bi++) {
        const X64Const *c = &f->consts[bi];
        u32 p2 = c->align >= 16 ? 4 : c->align >= 8 ? 3 : 2;
        u32 k;

        buf_printf(out, "\t.p2align\t%u\n.LCf%u_%u:\n", p2, fidx, bi);
        for (k = 0; k < c->size && k < 8; k++)
            buf_printf(out, "\t.byte\t%u\n", (u32)(c->lo >> (8 * k)) & 0xff);
        for (; k < c->size; k++)
            buf_printf(out, "\t.byte\t%u\n",
                       (u32)(c->hi >> (8 * (k - 8))) & 0xff);
        /* pad f80 entries to 16 so the next entry's alignment is cheap */
        if (c->size == 10)
            buf_printf(out, "\t.zero\t6\n");
    }
}

/* --- data emission ----------------------------------------------------------
 */

/* Byte image + reloc list, emitted NUMERICALLY. Relocs are 8-byte
 * `.quad sym+addend` splices; zero runs of 16+ collapse to `.zero`. */
static void emit_image(const IrModule *m, const IrGlobal *g, Buf *out)
{
    u64 off = 0;
    u32 ri = 0;

    while (off < g->size) {
        if (ri < g->nrelocs && g->relocs[ri].offset == off) {
            const IrReloc *r = &g->relocs[ri++];

            buf_printf(out, "\t.quad\t%s", m->syms[r->symbol]);
            if (r->addend)
                buf_printf(out, "%+lld", (long long)r->addend);
            buf_printf(out, "\n");
            off += 8;
            continue;
        }
        {
            /* Zero-run scan up to the next reloc. */
            u64 lim = ri < g->nrelocs ? g->relocs[ri].offset : g->size;
            u64 z = off;

            while (z < lim && g->init[z] == 0)
                z++;
            if (z - off >= 16) {
                buf_printf(out, "\t.zero\t%llu\n",
                           (unsigned long long)(z - off));
                off = z;
                continue;
            }
        }
        buf_printf(out, "\t.byte\t%u\n", g->init[off]);
        off++;
    }
}

/* The section a global lands in. `section("name")` replaces the default and
 * takes "aw": we place const globals in .data today, so a named data section
 * being writable is consistent with that rather than a new divergence -- the
 * missing .rodata is a separate, pre-existing gap.
 *
 * The name also forces PROGBITS. gcc emits `.section .s,"aw"` then `.zero 4`
 * for an UNINITIALIZED object there rather than a .bss reservation or a
 * .comm, because the section the author named is where the bytes must be. */
static void emit_global_section(Buf *out, const IrGlobal *g, bool zero_init)
{
    if (g->section) {
        buf_printf(out, "\t.section\t%s,\"aw\"\n", g->section);
        return;
    }
    if (zero_init)
        buf_printf(out, "\t.section\t%s\n",
                   g->is_tls ? ".tbss,\"awT\",@nobits" : ".bss");
    else if (g->is_tls)
        buf_printf(out, "\t.section\t.tdata,\"awT\",@progbits\n");
    else
        buf_printf(out, "\t.data\n");
}

void x64_emit_globals(const IrModule *m, Buf *out)
{
    u32 i;

    /* Aliases first: `.set` needs no section and no alignment, and emitting
     * them before any `.data`/`.bss` switch keeps the output free of a
     * section change that means nothing. gas resolves a `.set` whose target
     * appears later in the file, so the order is free. */
    for (i = 0; i < m->naliases; i++) {
        const IrAlias *a = &m->aliases[i];

        /* An INTERNAL alias gets no binding directive at all -- gcc emits a
         * bare `.set`, and adding `.local` would be a claim the target's own
         * `.local` already makes. */
        if (a->linkage != IRLINK_INTERNAL)
            buf_printf(out, a->is_weak ? "\t.weak\t%s\n" : "\t.globl\t%s\n",
                       a->name);
        emit_symbol_attrs(out, a->name, a->visibility);
        buf_printf(out, "\t.set\t%s,%s\n", a->name, a->target);
    }
    for (i = 0; i < m->nglobals; i++) {
        const IrGlobal *g = &m->globals[i];
        u32 p2 = 0;
        u32 a;

        for (a = g->align; a > 1; a >>= 1)
            p2++;
        if (g->is_tentative && !g->is_tls && !g->section) {
            /* -fcommon tentative: the linker merges. */
            buf_printf(out, "\t.comm\t%s,%llu,%u\n", g->name,
                       (unsigned long long)g->size, g->align);
            continue;
        }
        if (!g->init) {
            /* zero-initialized: BSS -- .tbss for a thread-local, whose
             * initial image is the TEMPLATE each new thread is given rather
             * than storage anyone writes to directly. The T flag is what
             * marks a section thread-local to the linker.
             *
             * A tentative thread-local cannot go through .comm, which has no
             * way to say "thread-local"; it becomes an ordinary .tbss
             * definition, which is what gcc does too. */
            if (g->linkage == IRLINK_INTERNAL)
                buf_printf(out, "\t.local\t%s\n", g->name);
            else
                buf_printf(out, g->is_weak ? "\t.weak\t%s\n" : "\t.globl\t%s\n",
                           g->name);
            emit_symbol_attrs(out, g->name, g->visibility);
            emit_global_section(out, g, true);
            buf_printf(out, "\t.p2align\t%u\n", p2);
            buf_printf(out, "\t.type\t%s, %s\n", g->name,
                       g->is_tls ? "@tls_object" : "@object");
            buf_printf(out, "\t.size\t%s, %llu\n", g->name,
                       (unsigned long long)g->size);
            buf_printf(out, "%s:\n\t.zero\t%llu\n", g->name,
                       (unsigned long long)g->size);
            continue;
        }
        if (g->linkage == IRLINK_INTERNAL)
            buf_printf(out, "\t.local\t%s\n", g->name);
        else
            buf_printf(out, g->is_weak ? "\t.weak\t%s\n" : "\t.globl\t%s\n",
                       g->name);
        emit_symbol_attrs(out, g->name, g->visibility);
        emit_global_section(out, g, false);
        buf_printf(out, "\t.p2align\t%u\n", p2);
        buf_printf(out, "\t.type\t%s, %s\n", g->name,
                   g->is_tls ? "@tls_object" : "@object");
        buf_printf(out, "\t.size\t%s, %llu\n", g->name,
                   (unsigned long long)g->size);
        buf_printf(out, "%s:\n", g->name);
        emit_image(m, g, out);
    }
}
