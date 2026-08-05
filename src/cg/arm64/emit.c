#include "cg/arm64/mir.h"

#include <string.h>

#include "diag.h"
#include "util/buf.h"

/* Sprint 49: post-allocation A64 MIR to GNU-syntax aarch64 assembly.
 *
 * The structure mirrors x86_64/emit.c deliberately: one table-driven
 * instruction printer, per-function .rodata for jump tables and the constant
 * pool, and numeric data emission (no .ascii dialects — the byte-for-byte
 * determinism law from Sprint 0 applies to both backends).
 *
 * Global addressing is the one genuinely new thing. An aarch64 address is
 * formed from a PAGE and a page offset:
 *
 *     adrp x0, sym              // R_AARCH64_ADR_PREL_PG_HI21
 *     add  x0, x0, #:lo12:sym   // R_AARCH64_ADD_ABS_LO12_NC
 *
 * adrp computes page(sym) - page(pc) into bits 32:12, reaching +/-4GB; the
 * add supplies the low twelve bits. The folded form that puts :lo12: inside a
 * load is NOT used here: its immediate field is SCALED by the access size, so
 * it is only legal when the symbol's alignment is at least that size, and an
 * underaligned extern (a packed struct, say) would assemble to a silently
 * wrong address. The add form always works, so correctness first; the fold is
 * an optimization with a precondition the emitter cannot check locally. */

typedef struct Emit {
    Buf *out;
    const A64Func *f;
    const IrModule *m;
    u32 fidx;
    u32 atomic_seq; /* names the labels inside each expanded ll/sc loop */
} Emit;

static const char *rn(A64Reg r, u8 sf)
{
    if (!r.id || !r.physical)
        CGF_ICE("arm64 emit: a virtual register reached emission");
    return a64_phys_name((u8)(r.id - 1), sf);
}

static bool reg_is_fp(A64Reg r)
{
    return r.physical && r.id - 1 >= A64_V0 && r.id - 1 <= A64_V31;
}

static const char *sym_name(const Emit *e, u32 id)
{
    if (!e->m || !id || id > e->m->nsyms)
        CGF_ICE("arm64 emit: symbol operand %u is out of range", id);
    return e->m->syms[id - 1];
}

/* Memory operands. A scaled offset is what the assembler wants written as a
 * plain displacement; the unscaled (ldur/stur) form is chosen by the
 * mnemonic, not by the operand text. */
static void pmem(Emit *e, const A64Mem *m, u8 sf)
{
    buf_printf(e->out, "[%s", rn(m->base, A64_SF64));
    if (m->index.id) {
        const char *ext = m->mode == A64_ADDR_REG_UXTW   ? "uxtw"
                          : m->mode == A64_ADDR_REG_SXTW ? "sxtw"
                                                         : "lsl";

        buf_printf(
            e->out, ", %s, %s",
            rn(m->index, m->mode == A64_ADDR_REG_LSL ? A64_SF64 : A64_SF32),
            ext);
        /* `lsl` REQUIRES its amount spelled even when zero; the extending
         * forms may omit it. GNU as rejects a bare `lsl`. */
        if (m->shift || m->mode == A64_ADDR_REG_LSL)
            buf_printf(e->out, " #%u", m->shift);
    } else if (m->offset && m->mode != A64_ADDR_POST) {
        buf_printf(e->out, ", #%lld", (long long)m->offset);
    }
    buf_printf(e->out, "]");
    if (m->mode == A64_ADDR_PRE)
        buf_printf(e->out, "!");
    else if (m->mode == A64_ADDR_POST)
        buf_printf(e->out, ", #%lld", (long long)m->offset);
    (void)sf;
}

static void poper(Emit *e, const A64Operand *o, u8 sf)
{
    switch (o->kind) {
    case A64O_REG:
        buf_printf(e->out, "%s", rn(o->reg, sf));
        break;
    case A64O_IMM:
        buf_printf(e->out, "#%lld", (long long)o->imm);
        break;
    case A64O_MEM:
        pmem(e, &o->mem, sf);
        break;
    case A64O_LABEL:
        buf_printf(e->out, ".Lf%u_%u", e->fidx, o->id);
        break;
    case A64O_SYM:
        buf_printf(e->out, "%s", sym_name(e, o->id));
        break;
    default:
        CGF_ICE("arm64 emit: operand kind %u has no spelling", o->kind);
    }
}

/* `n` operands, comma separated, each printed at the instruction's width
 * except memory operands, which always name an x-register base. */
static void emit_ops(Emit *e, const A64Inst *in, u32 first, u32 n, u8 sf)
{
    u32 i;

    for (i = first; i < first + n && i < in->nops; i++) {
        buf_printf(e->out, i == first ? "\t" : ", ");
        poper(e, &in->ops[i], sf);
    }
}

static void emit_simple(Emit *e, const A64Inst *in, const char *mnemonic, u8 sf)
{
    buf_printf(e->out, "\t%s", mnemonic);
    emit_ops(e, in, 0, in->nops, sf);
    buf_printf(e->out, "\n");
}

/* Load/store mnemonics. IR loads never sign-extend — a narrowing load is
 * followed by an explicit IR_SEXT when the source type is signed — so the
 * zero-extending forms are always right, and picking ldrsb here would be a
 * silent miscompile for unsigned char. */
static const char *mem_mnemonic(bool store, bool fp, u8 size)
{
    if (fp)
        return store ? "str" : "ldr";
    switch (size) {
    case 1:
        return store ? "strb" : "ldrb";
    case 2:
        return store ? "strh" : "ldrh";
    case 4:
    case 8:
        return store ? "str" : "ldr";
    default:
        CGF_ICE("arm64 emit: memory access of %u bytes", size);
    }
}

/* A sub-word GP access names its W view whatever the instruction's declared
 * width; the byte/halfword mnemonic carries the size. */
static u8 mem_reg_sf(const A64Inst *in, bool fp, u8 size)
{
    if (fp)
        return size == 4 ? A64_SF32 : A64_SF64;
    return size >= 8 ? A64_SF64 : (u8)(size == 4 ? in->sf : A64_SF32);
}

/* --- atomics ---------------------------------------------------------------
 *
 * The armv8.0 baseline has no LSE, so every atomic read-modify-write is an
 * exclusive loop. Ordering: ldaxr provides the acquire half and stlxr the
 * release half, which together give seq_cst under the AArch64 mapping — the
 * one Sprint 20 fixed as policy, where weaker orders are accepted and
 * strengthened rather than implemented separately.
 *
 * UPGRADE(armv8.1-lse): ldadd/swp/cas collapse each of these to a single
 * instruction. They must stay behind real feature routing, because emitting
 * them unconditionally would fault on an armv8.0 part.
 *
 * x12 and x13 are withheld from allocation for these expansions: the loop
 * needs a temporary and a status register, and taking either from the reload
 * scratches could collide with a spilled operand of this very instruction. */
#define A64_ATOMIC_TMP A64_X12
#define A64_ATOMIC_STATUS A64_X13

static const char *ex_suffix(u8 size)
{
    switch (size) {
    case 1:
        return "b";
    case 2:
        return "h";
    case 4:
    case 8:
        return "";
    default:
        CGF_ICE("arm64 emit: atomic access of %u bytes", size);
    }
}

/* The exclusive forms name a W register for every size below eight bytes;
 * the mnemonic suffix carries the width. */
static u8 ex_sf(u8 size)
{
    return size == 8 ? A64_SF64 : A64_SF32;
}

static const char *rmw_mnemonic(i64 op)
{
    switch (op) {
    case RMW_ADD:
        return "add";
    case RMW_SUB:
        return "sub";
    case RMW_AND:
        return "and";
    case RMW_OR:
        return "orr";
    case RMW_XOR:
        return "eor";
    case RMW_XCHG:
        return NULL; /* no arithmetic: the new value IS the operand */
    default:
        CGF_ICE("arm64 emit: unknown atomicrmw operation %lld", (long long)op);
    }
}

static void emit_atomic_rmw(Emit *e, const A64Inst *in, u32 seq)
{
    const A64Mem *m = &in->ops[1].mem;
    const char *sfx = ex_suffix(m->size);
    u8 sf = ex_sf(m->size);
    const char *base = rn(m->base, A64_SF64);
    const char *dst = rn(in->ops[0].reg, sf);
    const char *val = rn(in->ops[2].reg, sf);
    const char *tmp = a64_phys_name(A64_ATOMIC_TMP, sf);
    const char *status = a64_phys_name(A64_ATOMIC_STATUS, A64_SF32);
    const char *op = rmw_mnemonic(in->ops[3].imm);

    buf_printf(e->out, ".Lat%u_%u:\n", e->fidx, seq);
    buf_printf(e->out, "\tldaxr%s\t%s, [%s]\n", sfx, dst, base);
    if (op)
        buf_printf(e->out, "\t%s\t%s, %s, %s\n", op, tmp, dst, val);
    buf_printf(e->out, "\tstlxr%s\t%s, %s, [%s]\n", sfx, status, op ? tmp : val,
               base);
    buf_printf(e->out, "\tcbnz\t%s, .Lat%u_%u\n", status, e->fidx, seq);
}

static void emit_atomic_cas(Emit *e, const A64Inst *in, u32 seq)
{
    const A64Mem *m = &in->ops[1].mem;
    const char *sfx = ex_suffix(m->size);
    u8 sf = ex_sf(m->size);
    const char *base = rn(m->base, A64_SF64);
    const char *dst = rn(in->ops[0].reg, sf);
    const char *expected = rn(in->ops[2].reg, sf);
    const char *desired = rn(in->ops[3].reg, sf);
    const char *status = a64_phys_name(A64_ATOMIC_STATUS, A64_SF32);

    buf_printf(e->out, ".Lat%u_%u:\n", e->fidx, seq);
    buf_printf(e->out, "\tldaxr%s\t%s, [%s]\n", sfx, dst, base);
    buf_printf(e->out, "\tcmp\t%s, %s\n", dst, expected);
    buf_printf(e->out, "\tb.ne\t.Lax%u_%u\n", e->fidx, seq);
    buf_printf(e->out, "\tstlxr%s\t%s, %s, [%s]\n", sfx, status, desired, base);
    buf_printf(e->out, "\tcbnz\t%s, .Lat%u_%u\n", status, e->fidx, seq);
    buf_printf(e->out, "\tb\t.Lad%u_%u\n", e->fidx, seq);
    /* The comparison failed, so no stlxr ran and the monitor is still armed.
     * Leaving it armed is legal but antisocial: it can make an unrelated
     * exclusive sequence fail spuriously, so the failure edge clears it. */
    buf_printf(e->out, ".Lax%u_%u:\n\tclrex\n", e->fidx, seq);
    buf_printf(e->out, ".Lad%u_%u:\n", e->fidx, seq);
}

static void emit_mem(Emit *e, const A64Inst *in, bool store)
{
    const A64Operand *value = &in->ops[0];
    const A64Operand *addr = &in->ops[1];
    bool fp;
    u8 size, sf;

    if (in->nops != 2 || value->kind != A64O_REG || addr->kind != A64O_MEM)
        CGF_ICE("arm64 emit: malformed memory instruction");
    fp = reg_is_fp(value->reg);
    size = addr->mem.size ? addr->mem.size : (u8)(in->sf == A64_SF32 ? 4 : 8);
    sf = mem_reg_sf(in, fp, size);
    if (in->flags & A64IF_ATOMIC) {
        /* Sequentially consistent load/store. Neither ldar nor stlr has an
         * offset field, so selection hands these a bare base — a folded
         * displacement here would be unencodable rather than merely slow. */
        if (addr->mem.offset || addr->mem.index.id)
            CGF_ICE("arm64 emit: an atomic access cannot carry an offset");
        buf_printf(e->out, "\t%s%s\t%s, [%s]\n", store ? "stlr" : "ldar",
                   ex_suffix(size), rn(value->reg, sf),
                   rn(addr->mem.base, A64_SF64));
        return;
    }
    buf_printf(e->out, "\t%s\t%s, ", mem_mnemonic(store, fp, size),
               rn(value->reg, sf));
    pmem(e, &addr->mem, sf);
    buf_printf(e->out, "\n");
}

static void emit_pair(Emit *e, const A64Inst *in, bool store)
{
    const A64Operand *addr = &in->ops[2];
    bool fp;
    u8 sf;

    if (in->nops != 3 || addr->kind != A64O_MEM)
        CGF_ICE("arm64 emit: malformed load/store pair");
    fp = reg_is_fp(in->ops[0].reg);
    sf = fp ? A64_SF64 : (u8)in->sf;
    buf_printf(e->out, "\t%s\t%s, %s, ", store ? "stp" : "ldp",
               rn(in->ops[0].reg, sf), rn(in->ops[1].reg, sf));
    pmem(e, &addr->mem, sf);
    buf_printf(e->out, "\n");
}

/* `adrp` plus `add #:lo12:` — see the file header for why the load-folded
 * form is deliberately not used. */
static void emit_addr(Emit *e, const A64Inst *in)
{
    const char *sym;

    if (in->nops != 2 || in->ops[0].kind != A64O_REG ||
        in->ops[1].kind != A64O_SYM)
        CGF_ICE("arm64 emit: malformed global address");
    sym = sym_name(e, in->ops[1].id);
    buf_printf(e->out, "\tadrp\t%s, %s\n", rn(in->ops[0].reg, A64_SF64), sym);
    buf_printf(e->out, "\tadd\t%s, %s, #:lo12:%s\n",
               rn(in->ops[0].reg, A64_SF64), rn(in->ops[0].reg, A64_SF64), sym);
}

static void emit_call(Emit *e, const A64Inst *in)
{
    const A64CallInfo *c = in->call;

    if (!c)
        CGF_ICE("arm64 emit: call without metadata");
    switch (c->callee_ref_kind) {
    case FUNCREF_INDIRECT:
        buf_printf(e->out, "\tblr\t%s\n", rn(c->indirect, A64_SF64));
        return;
    case FUNCREF_INTERNAL:
        if (!e->m || c->callee_id >= e->m->nfuncs)
            CGF_ICE("arm64 emit: internal callee %u is out of range",
                    c->callee_id);
        buf_printf(e->out, "\tbl\t%s\n", e->m->funcs[c->callee_id].name);
        return;
    default:
        if (!e->m || c->callee_id >= e->m->nsyms)
            CGF_ICE("arm64 emit: external callee %u is out of range",
                    c->callee_id);
        buf_printf(e->out, "\tbl\t%s\n", e->m->syms[c->callee_id]);
        return;
    }
}

/* FMOV's immediate operand is an 8-bit ENCODING, not a value: isel stores
 * what a64_fp_imm_encode produced. GNU as wants the value spelled, so this
 * decodes it back.
 *
 * imm8 = a:b:c:d:e:f:g:h denotes (-1)^a * (1 + efgh/16) * 2^r, where the
 * exponent r is cd-3 when b is set and cd+1 when it is clear — so r spans
 * [-3,4] and every encodable value is (16+efgh) * 2^(r-4). The denominator
 * is therefore a power of two no larger than 128, which divides 10^7
 * exactly, so the decimal expansion is EXACT and computed in integers.
 * Reaching for a host double here would both violate the no-host-FPU law
 * and risk printing a value the assembler re-parses differently. */
static void emit_fp_imm(Emit *e, i64 encoded)
{
    u32 imm8 = (u32)encoded & 0xffu;
    u32 a = (imm8 >> 7) & 1u;
    u32 b = (imm8 >> 6) & 1u;
    i32 cd = (i32)((imm8 >> 4) & 3u);
    u32 efgh = imm8 & 0xfu;
    i32 r = b ? cd - 3 : cd + 1;
    u64 num = 16u + efgh;
    u32 shift = (u32)(4 - r); /* 0..7 */
    u64 scaled = num * 10000000ull >> shift;
    u64 whole = scaled / 10000000ull;
    u64 frac = scaled % 10000000ull;

    if ((u32)encoded > 0xffu)
        CGF_ICE("arm64 emit: FP immediate %lld is not an 8-bit encoding",
                (long long)encoded);
    buf_printf(e->out, "#%s%llu", a ? "-" : "", (unsigned long long)whole);
    if (frac) {
        char digits[8];
        u32 i, last = 0;

        for (i = 0; i < 7; i++) {
            digits[i] = (char)('0' + (frac / 1000000ull));
            frac = (frac % 1000000ull) * 10ull;
            if (digits[i] != '0')
                last = i + 1;
        }
        buf_printf(e->out, ".");
        for (i = 0; i < last; i++)
            buf_printf(e->out, "%c", digits[i]);
    } else {
        buf_printf(e->out, ".0");
    }
}

static const char *plain_mnemonic(u16 op)
{
    switch (op) {
    case A64_OP_MOV:
        return "mov";
    case A64_OP_MOVZ:
        return "movz";
    case A64_OP_MOVN:
        return "movn";
    case A64_OP_MOVK:
        return "movk";
    case A64_OP_ADD:
        return "add";
    case A64_OP_ADDS:
        return "adds";
    case A64_OP_SUB:
        return "sub";
    case A64_OP_SUBS:
        return "subs";
    case A64_OP_AND:
        return "and";
    case A64_OP_ANDS:
        return "ands";
    case A64_OP_ORR:
        return "orr";
    case A64_OP_EOR:
        return "eor";
    case A64_OP_LSL:
        return "lsl";
    case A64_OP_LSR:
        return "lsr";
    case A64_OP_ASR:
        return "asr";
    case A64_OP_MUL:
        return "mul";
    case A64_OP_MADD:
        return "madd";
    case A64_OP_MSUB:
        return "msub";
    case A64_OP_MNEG:
        return "mneg";
    case A64_OP_SDIV:
        return "sdiv";
    case A64_OP_UDIV:
        return "udiv";
    case A64_OP_SMULL:
        return "smull";
    case A64_OP_UMULL:
        return "umull";
    case A64_OP_SMULH:
        return "smulh";
    case A64_OP_UMULH:
        return "umulh";
    case A64_OP_FMOV:
        return "fmov";
    case A64_OP_FADD:
        return "fadd";
    case A64_OP_FSUB:
        return "fsub";
    case A64_OP_FMUL:
        return "fmul";
    case A64_OP_FDIV:
        return "fdiv";
    case A64_OP_FSQRT:
        return "fsqrt";
    case A64_OP_FNEG:
        return "fneg";
    case A64_OP_FABS:
        return "fabs";
    case A64_OP_FCMP:
        return "fcmp";
    default:
        return NULL;
    }
}

/* SMULL/UMULL take W sources and an X destination; the mixed widths are the
 * whole point of the instruction, so they cannot ride the uniform path. */
static bool is_widening_mul(u16 op)
{
    return op == A64_OP_SMULL || op == A64_OP_UMULL;
}

/* A64 conditional branches carry BOTH edges: selection records the taken
 * target and the fallthrough target, because the block order is not fixed
 * until emission. Printing only the first one silently drops the false edge
 * and control runs into whatever block happens to come next — assembly that
 * assembles cleanly and computes the wrong answer.
 *
 * `first` is the operand index of the taken label; the one after it is the
 * untaken label, which needs no branch when it is already the next block. */
static void emit_cond_targets(Emit *e, const A64Inst *in, u32 first,
                              u32 next_bb)
{
    const A64Operand *taken = &in->ops[first];
    const A64Operand *untaken =
        first + 1 < in->nops ? &in->ops[first + 1] : NULL;

    if (taken->kind != A64O_LABEL)
        CGF_ICE("arm64 emit: conditional branch without a taken label");
    poper(e, taken, A64_SF64);
    buf_printf(e->out, "\n");
    if (!untaken) {
        /* A switch compare tree branches MID-BLOCK: each `b.eq case` falls
         * through to the next comparison in the same block, so there is no
         * second edge to name. Only a terminator carries two. */
        return;
    }
    if (untaken->kind != A64O_LABEL)
        CGF_ICE("arm64 emit: conditional branch fallthrough is not a label");
    if (untaken->id == next_bb)
        return;
    buf_printf(e->out, "\tb\t");
    poper(e, untaken, A64_SF64);
    buf_printf(e->out, "\n");
}

static void emit_inst(Emit *e, const A64Inst *in, u32 next_bb)
{
    const char *mn;
    u8 sf = in->sf;

    switch (in->op) {
    case A64_OP_LOAD:
        emit_mem(e, in, false);
        return;
    case A64_OP_STORE:
        emit_mem(e, in, true);
        return;
    case A64_OP_ATOMIC_LLSC:
        emit_atomic_rmw(e, in, e->atomic_seq++);
        return;
    case A64_OP_ATOMIC_CAS:
        emit_atomic_cas(e, in, e->atomic_seq++);
        return;
    case A64_OP_LDP:
        emit_pair(e, in, false);
        return;
    case A64_OP_STP:
        emit_pair(e, in, true);
        return;
    case A64_OP_ADDR:
        emit_addr(e, in);
        return;
    case A64_OP_CALL:
        emit_call(e, in);
        return;
    case A64_OP_RET:
        buf_printf(e->out, "\tret\n");
        return;
    case A64_OP_BR:
        buf_printf(e->out, "\tbr\t%s\n", rn(in->ops[0].reg, A64_SF64));
        return;
    case A64_OP_UNREACHABLE:
        /* A trapping encoding, never a fallthrough: reaching here is UB and
         * must stop rather than wander into the next function. */
        buf_printf(e->out, "\tbrk\t#1\n");
        return;
    case A64_OP_B:
        if (in->nops && in->ops[0].kind == A64O_LABEL &&
            in->ops[0].id == next_bb)
            return; /* falls through to its own target */
        buf_printf(e->out, "\tb\t");
        poper(e, &in->ops[0], A64_SF64);
        buf_printf(e->out, "\n");
        return;
    case A64_OP_BCOND:
        buf_printf(e->out, "\tb.%s\t", a64_cond_name(in->cond));
        emit_cond_targets(e, in, 0, next_bb);
        return;
    case A64_OP_CBZ:
    case A64_OP_CBNZ:
        buf_printf(e->out, "\t%s\t%s, ", in->op == A64_OP_CBZ ? "cbz" : "cbnz",
                   rn(in->ops[0].reg, sf));
        emit_cond_targets(e, in, 1, next_bb);
        return;
    case A64_OP_TBZ:
    case A64_OP_TBNZ:
        buf_printf(e->out, "\t%s\t%s, #%lld, ",
                   in->op == A64_OP_TBZ ? "tbz" : "tbnz",
                   rn(in->ops[0].reg, sf), (long long)in->ops[1].imm);
        emit_cond_targets(e, in, 2, next_bb);
        return;
    case A64_OP_CSEL:
    case A64_OP_CSINC:
    case A64_OP_CSINV:
    case A64_OP_CSNEG:
        buf_printf(e->out, "\t%s", a64_op_name(in->op));
        emit_ops(e, in, 0, 3, sf);
        buf_printf(e->out, ", %s\n", a64_cond_name(in->cond));
        return;
    case A64_OP_CSET:
    case A64_OP_CSETM:
        buf_printf(e->out, "\t%s\t%s, %s\n", a64_op_name(in->op),
                   rn(in->ops[0].reg, sf), a64_cond_name(in->cond));
        return;
    case A64_OP_FCSEL:
        buf_printf(e->out, "\tfcsel");
        emit_ops(e, in, 0, 3, sf);
        buf_printf(e->out, ", %s\n", a64_cond_name(in->cond));
        return;
    case A64_OP_SCVTF:
    case A64_OP_UCVTF:
    case A64_OP_FCVTZS:
    case A64_OP_FCVTZU:
    case A64_OP_FCVT:
        /* Independent destination and source widths — the reason A64Inst
         * carries src_sf at all. */
        buf_printf(e->out, "\t%s\t%s, %s\n", a64_op_name(in->op),
                   rn(in->ops[0].reg, sf), rn(in->ops[1].reg, in->src_sf));
        return;
    default:
        break;
    }
    if (is_widening_mul(in->op)) {
        buf_printf(e->out, "\t%s\t%s, %s, %s\n", a64_op_name(in->op),
                   rn(in->ops[0].reg, A64_SF64), rn(in->ops[1].reg, A64_SF32),
                   rn(in->ops[2].reg, A64_SF32));
        return;
    }
    if (in->op == A64_OP_FMOV && in->nops == 2 && in->ops[1].kind == A64O_IMM) {
        buf_printf(e->out, "\tfmov\t%s, ", rn(in->ops[0].reg, sf));
        emit_fp_imm(e, in->ops[1].imm);
        buf_printf(e->out, "\n");
        return;
    }
    mn = plain_mnemonic(in->op);
    if (!mn)
        CGF_ICE("arm64 emit: '%s' has no emission rule", a64_op_name(in->op));
    /* MOVZ/MOVN/MOVK carry their shift as a third operand; the assembler
     * wants it spelled `lsl #n` rather than as a bare immediate. */
    if ((in->op == A64_OP_MOVZ || in->op == A64_OP_MOVN ||
         in->op == A64_OP_MOVK) &&
        in->nops == 3) {
        buf_printf(e->out, "\t%s\t%s, #%lld", mn, rn(in->ops[0].reg, sf),
                   (long long)in->ops[1].imm);
        if (in->ops[2].imm)
            buf_printf(e->out, ", lsl #%lld", (long long)in->ops[2].imm);
        buf_printf(e->out, "\n");
        return;
    }
    emit_simple(e, in, mn, sf);
}

void a64_emit_function(const A64Func *f, const IrModule *m, u32 fidx,
                       u8 linkage, Buf *out)
{
    Emit e;
    u32 bi, i;

    if (!f->allocated)
        CGF_ICE("arm64 emit: '%s' reached emission unallocated", f->name);
    e.out = out;
    e.f = f;
    e.m = m;
    e.fidx = fidx;
    e.atomic_seq = 0;

    buf_printf(out, "\t.text\n");
    if (linkage != IRLINK_INTERNAL)
        buf_printf(out, "\t.globl\t%s\n", f->name);
    else
        buf_printf(out, "\t.local\t%s\n", f->name);
    /* Every A64 instruction is four bytes, so the natural function alignment
     * is 4; GNU as would otherwise leave the previous section's alignment. */
    buf_printf(out, "\t.p2align\t2\n");
    buf_printf(out, "\t.type\t%s, @function\n", f->name);
    buf_printf(out, "%s:\n", f->name);
    for (bi = 0; bi < f->nblocks; bi++) {
        const A64Block *b = &f->blocks[bi];

        if (bi)
            buf_printf(out, ".Lf%u_%u:\n", fidx, bi + 1);
        for (i = 0; i < b->n; i++)
            emit_inst(&e, &b->insts[i], bi + 2);
    }
    buf_printf(out, "\t.size\t%s, .-%s\n", f->name, f->name);
}

/* --- data emission ----------------------------------------------------------
 *
 * Byte image plus relocation list, emitted NUMERICALLY: no .ascii dialects,
 * because Sprint 0's byte-for-byte determinism law applies to both backends
 * and escape spelling is exactly where assemblers disagree. Relocations are
 * eight-byte `.quad sym+addend` splices; zero runs of sixteen or more
 * collapse to `.zero`. Identical in shape to the x86 emitter on purpose. */
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

void a64_emit_globals(const IrModule *m, Buf *out)
{
    u32 i;

    for (i = 0; i < m->nglobals; i++) {
        const IrGlobal *g = &m->globals[i];
        u32 p2 = 0;
        u32 a;

        for (a = g->align; a > 1; a >>= 1)
            p2++;
        if (g->is_tentative) {
            buf_printf(out, "\t.comm\t%s,%llu,%u\n", g->name,
                       (unsigned long long)g->size, g->align);
            continue;
        }
        if (g->linkage == IRLINK_INTERNAL)
            buf_printf(out, "\t.local\t%s\n", g->name);
        else
            buf_printf(out, "\t.globl\t%s\n", g->name);
        buf_printf(out, "\t.section\t%s\n", g->init ? ".data" : ".bss");
        buf_printf(out, "\t.p2align\t%u\n", p2);
        buf_printf(out, "\t.type\t%s, @object\n", g->name);
        buf_printf(out, "\t.size\t%s, %llu\n", g->name,
                   (unsigned long long)g->size);
        buf_printf(out, "%s:\n", g->name);
        if (!g->init)
            buf_printf(out, "\t.zero\t%llu\n", (unsigned long long)g->size);
        else
            emit_image(m, g, out);
    }
}
