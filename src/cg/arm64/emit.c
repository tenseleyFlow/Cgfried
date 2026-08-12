#include "cg/arm64/mir.h"
#include "cg/data.h"
#include "cg/shared.h"

#include <string.h>

#include "diag.h"
#include "target.h"
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

/* Mach-O and ELF disagree about spelling, not about instructions.
 *
 *   symbols        Apple prefixes every C identifier with `_`
 *   page-relative  `sym@PAGE` / `sym@PAGEOFF` vs `:lo12:`
 *   sections       `__TEXT,__text,regular,pure_instructions` vs `.text`
 *   size and type  Mach-O has NEITHER `.type` nor `.size`
 *   zero data      `.zerofill SEG,SECT,name,size,align` vs `.bss` + `.zero`
 *   local labels   `l_` vs `.L`
 *   file scope     `.build_version` first, `.subsections_via_symbols` last
 *
 * One flag rather than a second emitter: the instruction printer below is
 * identical on both, and duplicating it to change punctuation is how the two
 * drift apart. */
typedef struct Emit {
    Buf *out;
    const A64Func *f;
    const IrModule *m;
    u32 fidx;
    u32 atomic_seq; /* names the labels inside each expanded ll/sc loop */
    bool apple;     /* Mach-O spelling; otherwise ELF */
    /* Symbol names are consumed by printf-style format strings, some of
     * which take two at once, so the `_` prefix needs somewhere to live.
     * A small ring keeps the call sites unchanged. */
    char symbuf[4][192];
    u32 symslot;
} Emit;

/* The target's spelling of `name`.
 *
 * Anonymous globals -- string literals, local statics, compound literals --
 * are minted by lowering with ELF-shaped `.L` names. Prefixing those blindly
 * gives `_.Lstr.0`, which assembles but is neither a C identifier nor
 * assembler-local, so it would reach the Mach-O symbol table as clutter.
 * They become `l_`-prefixed instead -- the private-extern form clang uses for
 * exactly these (`l_.str`), which ld64 resolves and then strips. A capital
 * `L` would make them assembler TEMPORARIES, forcing a section-relative
 * relocation that afs-as does not resolve. */
static const char *msym(Emit *e, const char *name)
{
    char *slot;

    if (ir_sym_name_is_exact_asm(name))
        return ir_sym_asm_spelling(name);
    if (!e->apple)
        return name;
    slot = e->symbuf[e->symslot];
    e->symslot = (e->symslot + 1) % 4;
    if (name[0] == '.' && name[1] == 'L')
        snprintf(slot, sizeof(e->symbuf[0]), "l_%s", name + 2);
    else
        snprintf(slot, sizeof(e->symbuf[0]), "_%s", name);
    return slot;
}

/* `L` and `l_` are NOT interchangeable on Mach-O, and which one is right
 * depends on what REFERENCES the label. ELF spells both `.L`.
 *
 * `L...` is an ASSEMBLER temporary that never reaches the symbol table, so a
 * relocation against it has to be resolved section-relative. `l_...` is a
 * private extern: it reaches the object and the LINKER strips it later.
 *
 * A BRANCH TARGET must be the former. Apple's assembler rejects a
 * conditional branch to `l_f0_2` with "conditional branch requires
 * assembler-local label", which is what the first draft here produced;
 * clang's own basic-block labels are `LBB0_2` for the same reason.
 *
 * Anything a RELOCATION names -- a string literal, the constant pool -- is
 * the latter. clang writes `l_.str`. Apple's assembler accepts capital `L`
 * there too, which is why it went unnoticed, but afs-as does not resolve a
 * section-relative Mach-O relocation and rejects it with "missing relocation
 * symbol 'Lstr.0'". Matching clang is both the fix and the right idiom. */
static const char *mlabel(const Emit *e)
{
    return e->apple ? "L" : ".L";
}

static const char *mprivate(const Emit *e)
{
    return e->apple ? "l_" : ".L";
}

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
            e->out, ", %s",
            rn(m->index, m->mode == A64_ADDR_REG_LSL ? A64_SF64 : A64_SF32));
        /* `lsl #0` is not merely redundant: on a byte access it sets the S
         * bit, which changes the ENCODING while leaving the semantics
         * identical, so gas and afs-as produce different bytes for the same
         * instruction. Writing the bare `[base, index]` form makes the two
         * agree. `lsl` also REQUIRES its amount spelled when present — GNU
         * as rejects a bare `lsl` — while the extending forms may omit it. */
        if (m->shift || m->mode != A64_ADDR_REG_LSL) {
            buf_printf(e->out, ", %s", ext);
            if (m->shift || m->mode == A64_ADDR_REG_LSL)
                buf_printf(e->out, " #%u", m->shift);
        }
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
        buf_printf(e->out, "%sf%u_%u", mlabel(e), e->fidx, o->id);
        break;
    case A64O_SYM:
        buf_printf(e->out, "%s", msym(e, sym_name(e, o->id)));
        break;
    case A64O_CPOOL:
        buf_printf(e->out, "%scp%u_%u", mprivate(e), e->fidx, o->id - 1);
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
        return size == 16 ? A64_SF128 : size == 4 ? A64_SF32 : A64_SF64;
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

/* Whether a symbol is DEFINED by this translation unit. The module carries
 * a body for every function it defines and an entry for every global it
 * defines; anything named only through the symbol table is external. Mach-O
 * needs the distinction because an undefined symbol is only reachable
 * through the GOT. */
/* Set once per module by the driver, before any function is emitted. A
 * parameter on every emit entry point would thread it through half a dozen
 * signatures that have nothing else to do with it. */
static A64PicLevel g_pic;

void a64_emit_set_pic(A64PicLevel pic)
{
    g_pic = pic;
}

static bool sym_defined_here(const Emit *e, const char *name)
{
    u32 i;

    if (!e->m)
        return false;
    for (i = 0; i < e->m->nglobals; i++)
        if (strcmp(e->m->globals[i].name, name) == 0)
            return true;
    for (i = 0; i < e->m->nfuncs; i++)
        if (strcmp(e->m->funcs[i].name, name) == 0)
            return true;
    return false;
}

/* An addend applied AFTER a GOT load, where it cannot ride the relocation.
 * `add`/`sub` take a 12-bit immediate, optionally shifted left by 12, so two
 * instructions cover +/- 16MiB; no C object offset comes close. */
static void emit_addr_addend(Emit *e, const char *reg, i64 addend)
{
    const char *op = addend < 0 ? "sub" : "add";
    u64 mag = addend < 0 ? (u64)(-(addend + 1)) + 1u : (u64)addend;

    if (mag >> 24)
        CGF_ICE("arm64 emit: address addend %lld does not fit two adds",
                (long long)addend);
    if (mag & 0xfffu)
        buf_printf(e->out, "\t%s\t%s, %s, #%llu\n", op, reg, reg,
                   (unsigned long long)(mag & 0xfffu));
    if (mag >> 12)
        buf_printf(e->out, "\t%s\t%s, %s, #%llu, lsl #12\n", op, reg, reg,
                   (unsigned long long)(mag >> 12));
}

/* `adrp` plus `add #:lo12:` — see the file header for why the load-folded
 * form is deliberately not used. */
static void emit_addr(Emit *e, const A64Inst *in)
{
    const char *sym;
    const char *reg;
    char addend[32];
    char cplabel[32];

    if ((in->nops != 2 && in->nops != 3) || in->ops[0].kind != A64O_REG ||
        (in->ops[1].kind != A64O_SYM && in->ops[1].kind != A64O_CPOOL))
        CGF_ICE("arm64 emit: malformed global address");
    if (in->ops[1].kind == A64O_CPOOL) {
        snprintf(cplabel, sizeof(cplabel), "%scp%u_%u", mprivate(e), e->fidx,
                 in->ops[1].id - 1);
        sym = cplabel;
    } else {
        sym = msym(e, sym_name(e, in->ops[1].id));
    }
    reg = rn(in->ops[0].reg, A64_SF64);
    /* An address constant may carry an addend -- `&g.member`, `&arr[k]`, or
     * anything the optimizer folded into one operand. It has to ride BOTH
     * halves of the pair: adrp computes page(S+A) and the lo12 adds
     * (S+A) & 0xfff, so spelling it on only one of them lands somewhere
     * between the two. ELF takes `sym+off` in both positions.  Mach-O puts
     * the relocation modifier before the arithmetic (`sym@PAGE+off`): Apple
     * as accepts either order, but the bundled afs-as accepts only this one. */
    addend[0] = '\0';
    if (in->nops == 3) {
        if (in->ops[2].kind != A64O_IMM)
            CGF_ICE("arm64 emit: global address addend is not an immediate");
        if (in->ops[2].imm)
            snprintf(addend, sizeof(addend), "%+lld",
                     (long long)in->ops[2].imm);
    }
    /* Same pair, different punctuation: Mach-O spells the halves `@PAGE`
     * and `@PAGEOFF` where ELF uses a bare symbol and `#:lo12:`. */
    if (e->apple) {
        /* ...unless the symbol is not DEFINED here, in which case Mach-O
         * routes it through the GOT even in a non-PIC build, and the pair
         * becomes adrp + LOAD rather than adrp + add. This is not an
         * optimization: ld64 emits no relocation that would let a direct
         * `add` reach an undefined symbol, so getting it wrong does not
         * produce a slower program, it produces a link error or a wrong
         * address. It applies to a function's ADDRESS as well as to data;
         * a direct `bl` is unaffected. Measured against clang. */
        if (in->ops[1].kind == A64O_SYM &&
            !sym_defined_here(e, sym_name(e, in->ops[1].id))) {
            /* An addend cannot ride a GOT relocation -- the slot holds the
             * symbol's base address and nothing else -- so it becomes a
             * separate add after the load, which is what clang does too. */
            buf_printf(e->out, "\tadrp\t%s, %s@GOTPAGE\n", reg, sym);
            buf_printf(e->out, "\tldr\t%s, [%s, %s@GOTPAGEOFF]\n", reg, reg,
                       sym);
            if (addend[0])
                emit_addr_addend(e, reg, in->ops[2].imm);
            return;
        }
        buf_printf(e->out, "\tadrp\t%s, %s@PAGE%s\n", reg, sym, addend);
        buf_printf(e->out, "\tadd\t%s, %s, %s@PAGEOFF%s\n", reg, reg, sym,
                   addend);
        return;
    }
    /* ELF, position-independent: the same adrp pair, but the second half is
     * a LOAD from the GOT slot rather than an add of the low bits. It costs
     * no extra instruction, which is why this lives in the emitter while the
     * x86 equivalent had to be in isel -- there, folding a preemptible
     * symbol into a memory operand is what stops being legal.
     *
     * Full PIC covers every external symbol, defined here or not: a
     * definition in this module is still interposable. PIE covers only
     * undefined ones -- measured against aarch64 gcc, which differs from
     * x86 gcc on exactly this row. */
    if (g_pic != A64_PIC_NONE && in->ops[1].kind == A64O_SYM) {
        IrSymBinding b = ir_sym_binding(e->m, in->ops[1].id);

        if (b.external && (g_pic == A64_PIC_FULL || !b.defined_here)) {
            buf_printf(e->out, "\tadrp\t%s, :got:%s\n", reg, sym);
            buf_printf(e->out, "\tldr\t%s, [%s, #:got_lo12:%s]\n", reg, reg,
                       sym);
            if (addend[0])
                emit_addr_addend(e, reg, in->ops[2].imm);
            return;
        }
    }
    buf_printf(e->out, "\tadrp\t%s, %s%s\n", reg, sym, addend);
    buf_printf(e->out, "\tadd\t%s, %s, #:lo12:%s%s\n", reg, reg, sym, addend);
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
        buf_printf(e->out, "\tbl\t%s\n",
                   msym(e, e->m->funcs[c->callee_id].name));
        return;
    default:
        if (!e->m || c->callee_id >= e->m->nsyms)
            CGF_ICE("arm64 emit: external callee %u is out of range",
                    c->callee_id);
        buf_printf(e->out, "\tbl\t%s\n", msym(e, e->m->syms[c->callee_id]));
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

/* A single lane names its ELEMENT size, not the register arrangement:
 * `umov w0, v1.s[2]`, never `v1.4s[2]`. */
static const char *lane_arrangement(u8 t)
{
    switch (t) {
    case IRT_V16I8:
        return "b";
    case IRT_V8I16:
        return "h";
    case IRT_V4I32:
    case IRT_V4F32:
        return "s";
    default:
        return "d";
    }
}

/* --- NEON ------------------------------------------------------------------
 *
 * The arrangement specifier comes from src_sf (the vector IrType), because
 * `add v0.4s` and `add v0.8h` are different instructions over the same
 * registers. Loads and stores need nothing special: ldr/str of a q register
 * never faults on alignment, so SSE2's aligned/unaligned split has no
 * counterpart to port. */
static const char *vec_mnemonic(u16 op)
{
    switch (op) {
    case A64_OP_VADD:
        return "add";
    case A64_OP_VSUB:
        return "sub";
    case A64_OP_VMUL:
        return "mul";
    case A64_OP_VAND:
        return "and";
    case A64_OP_VORR:
        return "orr";
    case A64_OP_VEOR:
        return "eor";
    case A64_OP_VFADD:
        return "fadd";
    case A64_OP_VFSUB:
        return "fsub";
    case A64_OP_VFMUL:
        return "fmul";
    case A64_OP_VFDIV:
        return "fdiv";
    default:
        return NULL;
    }
}

static const char *vname(A64Reg r)
{
    if (!r.id || !r.physical)
        CGF_ICE("arm64 emit: a virtual register reached emission");
    return a64_vec_name((u8)(r.id - 1));
}

static bool emit_neon(Emit *e, const A64Inst *in)
{
    const char *arr = a64_vec_arrangement(in->src_sf);
    const char *mn = vec_mnemonic(in->op);

    if (mn) {
        /* The bitwise forms take ONLY .8b or .16b: the element size means
         * nothing to them, and gas rejects `eor v0.4s`. Everything else
         * carries its real arrangement. */
        if (in->op == A64_OP_VAND || in->op == A64_OP_VORR ||
            in->op == A64_OP_VEOR)
            arr = "16b";
        /* NEON has no 64-bit vector multiply; the vectorizer must not have
         * formed one. Erroring beats emitting a mnemonic gas will reject
         * later with no explanation of where it came from. */
        if (in->op == A64_OP_VMUL &&
            (in->src_sf == IRT_V2I64 || in->src_sf == IRT_V2F64))
            CGF_ICE("arm64 emit: NEON has no 64-bit integer vector multiply");
        buf_printf(e->out, "\t%s\t%s.%s, %s.%s, %s.%s\n", mn,
                   vname(in->ops[0].reg), arr, vname(in->ops[1].reg), arr,
                   vname(in->ops[2].reg), arr);
        return true;
    }
    switch (in->op) {
    case A64_OP_VDUP:
        /* the logical/arithmetic bit-width of the source register follows
         * the lane, not the instruction */
        buf_printf(e->out, "\tdup\t%s.%s, %s\n", vname(in->ops[0].reg), arr,
                   rn(in->ops[1].reg, a64_vec_lane_sf(in->src_sf)));
        return true;
    case A64_OP_VDUPLANE:
        buf_printf(e->out, "\tdup\t%s.%s, %s.%s[0]\n", vname(in->ops[0].reg),
                   arr, vname(in->ops[1].reg), lane_arrangement(in->src_sf));
        return true;
    case A64_OP_VEXT:
        /* ext is always byte-addressed, whatever the element size. */
        buf_printf(e->out, "\text\t%s.16b, %s.16b, %s.16b, #%lld\n",
                   vname(in->ops[0].reg), vname(in->ops[1].reg),
                   vname(in->ops[2].reg), (long long)in->ops[3].imm);
        return true;
    case A64_OP_VUMOV:
        buf_printf(e->out, "\tumov\t%s, %s.%s[%lld]\n",
                   rn(in->ops[0].reg, a64_vec_lane_sf(in->src_sf)),
                   vname(in->ops[1].reg), lane_arrangement(in->src_sf),
                   (long long)in->ops[2].imm);
        return true;
    case A64_OP_VLANE:
        buf_printf(e->out, "\tmov\t%s, %s.%s[%lld]\n",
                   rn(in->ops[0].reg, a64_vec_lane_sf(in->src_sf)),
                   vname(in->ops[1].reg), lane_arrangement(in->src_sf),
                   (long long)in->ops[2].imm);
        return true;
    default:
        return false;
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
    case A64_OP_TLSADDR: {
        /* Local-exec on AAPCS64. The thread pointer is an architectural
         * register rather than a segment base, and the offset arrives in two
         * 12-bit halves:
         *
         *     mrs xN, tpidr_el0
         *     add xN, xN, #:tprel_hi12:sym, lsl #12
         *     add xN, xN, #:tprel_lo12_nc:sym
         *
         * R_AARCH64_TLSLE_ADD_TPREL_HI12 and ..._LO12_NC. `_nc` is
         * no-check: the pair is only correct together, so the low half must
         * not complain about the bits the high half carries. */
        const char *reg = rn(in->ops[0].reg, A64_SF64);
        const char *sym = msym(e, e->m->syms[in->ops[1].id - 1]);

        buf_printf(e->out, "\tmrs\t%s, tpidr_el0\n", reg);
        buf_printf(e->out, "\tadd\t%s, %s, #:tprel_hi12:%s, lsl #12\n", reg,
                   reg, sym);
        buf_printf(e->out, "\tadd\t%s, %s, #:tprel_lo12_nc:%s\n", reg, reg,
                   sym);
        if (in->nops > 2 && in->ops[2].imm)
            buf_printf(e->out, "\tadd\t%s, %s, #%lld\n", reg, reg,
                       (long long)in->ops[2].imm);
        return;
    }
    case A64_OP_CALL:
        emit_call(e, in);
        return;
    case A64_OP_RET:
        buf_printf(e->out, "\tret\n");
        return;
    case A64_OP_ASM:
        a64_emit_asm_text(e->out, e->m, (u32)in->ops[0].imm, in->asm_info);
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
    case A64_OP_FMADD:
    case A64_OP_FMSUB:
        buf_printf(e->out, "\t%s\t%s, %s, %s, %s\n", a64_op_name(in->op),
                   rn(in->ops[0].reg, sf), rn(in->ops[1].reg, sf),
                   rn(in->ops[2].reg, sf), rn(in->ops[3].reg, sf));
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
    if (emit_neon(e, in))
        return;
    if (is_widening_mul(in->op)) {
        buf_printf(e->out, "\t%s\t%s, %s, %s\n", a64_op_name(in->op),
                   rn(in->ops[0].reg, A64_SF64), rn(in->ops[1].reg, A64_SF32),
                   rn(in->ops[2].reg, A64_SF32));
        return;
    }
    /* There is no 128-bit `fmov`: a whole-register move is the NEON
     * `mov vD.16b, vN.16b`. binary128 values reach this path once
     * lower/f128.c has made their arithmetic soft-float -- the VALUE still
     * moves and spills as a q register. */
    if (in->op == A64_OP_FMOV && in->nops == 2 && in->ops[1].kind == A64O_REG &&
        sf == A64_SF128) {
        buf_printf(e->out, "\tmov\t%s.16b, %s.16b\n", vname(in->ops[0].reg),
                   vname(in->ops[1].reg));
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

/* GNU symbol attributes. ELF only: Mach-O spells weak binding
 * `.weak_definition` and has no `.hidden` at all (visibility there is
 * `.private_extern`), so rather than guess a mapping this emits nothing on
 * Apple -- the attributes are already refused for that target's hosted
 * builds, and a wrong directive is worse than an absent one. */
static void a64_symbol_attrs(const Emit *e, Buf *out, const char *name,
                             u8 visibility)
{
    if (e->apple)
        return;
    if (visibility && visibility != GNU_VIS_DEFAULT)
        buf_printf(out, "\t.%s\t%s\n", gnu_visibility_name(visibility), name);
}

void a64_emit_function(const A64Func *f, const IrModule *m, u32 fidx,
                       u8 linkage, Buf *out)
{
    Emit e;
    u32 bi, i;

    if (!f->allocated)
        CGF_ICE("arm64 emit: '%s' reached emission unallocated", f->name);
    memset(&e, 0, sizeof(e));
    e.out = out;
    e.f = f;
    e.m = m;
    e.fidx = fidx;
    e.atomic_seq = 0;
    e.apple = cgf_target_selected().kind == CGF_TARGET_ARM64_MACOS;

    {
        const IrFunc *sf = fidx < m->nfuncs ? &m->funcs[fidx] : NULL;

        /* `section("name")` names an ELF section. Mach-O's spelling is
         * SEGMENT,SECTION with attributes, so a single ELF-shaped name cannot
         * be reused there; refused by name rather than mangled into something
         * that assembles and lands somewhere else. */
        if (sf && sf->section && e.apple)
            CGF_ICE("arm64-macos: section(\"%s\") needs the Mach-O "
                    "SEGMENT,SECTION spelling (SEC-MACHO-001)",
                    sf->section);
        if (sf && sf->section)
            buf_printf(out, "\t.section\t%s,\"ax\",@progbits\n", sf->section);
        else
            buf_printf(out, e.apple ? "\t.section\t__TEXT,__text,regular,"
                                      "pure_instructions\n"
                                    : "\t.text\n");
    }
    if (linkage != IRLINK_INTERNAL)
        buf_printf(out,
                   (!e.apple && fidx < m->nfuncs && m->funcs[fidx].is_weak)
                       ? "\t.weak\t%s\n"
                       : "\t.globl\t%s\n",
                   msym(&e, f->name));
    else if (!e.apple)
        buf_printf(out, "\t.local\t%s\n", f->name);
    a64_symbol_attrs(&e, out, f->name,
                     fidx < m->nfuncs ? m->funcs[fidx].visibility : 0);
    /* Every A64 instruction is four bytes, so the natural function alignment
     * is 4; GNU as would otherwise leave the previous section's alignment. */
    buf_printf(out, "\t.p2align\t%u\n",
               cg_func_p2align(fidx < m->nfuncs ? &m->funcs[fidx] : NULL, 2));
    /* Mach-O has no `.type` and no `.size`: a symbol's extent comes from
     * `.subsections_via_symbols` and the next symbol, not a directive. */
    if (!e.apple)
        buf_printf(out, "\t.type\t%s, @function\n", f->name);
    buf_printf(out, "%s:\n", msym(&e, f->name));
    /* A LOCAL alias for the entry, named by the .eh_frame FDE. Naming the
     * global symbol there emits a PC-relative relocation against an
     * interposable symbol, which ld refuses when making a shared object --
     * the same reason x86 grew this label. */
    buf_printf(out, "%sfb%u:\n", mlabel(&e), fidx);
    if (f->debug_lines)
        buf_printf(out, "%sloc_%u_0:\n", mlabel(&e), fidx);
    for (bi = 0; bi < f->nblocks; bi++) {
        const A64Block *b = &f->blocks[bi];

        if (bi)
            buf_printf(out, "%sf%u_%u:\n", mlabel(&e), fidx, bi + 1);
        for (i = 0; i < b->n; i++) {
            if (f->debug_lines && b->insts[i].debug_label)
                buf_printf(out, "%sloc_%u_%u:\n", mlabel(&e), fidx,
                           b->insts[i].debug_label);
            emit_inst(&e, &b->insts[i], bi + 2);
        }
    }
    buf_printf(out, "%sfe%u_0:\n", mlabel(&e), fidx);
    if (!e.apple)
        buf_printf(out, "\t.size\t%s, .-%s\n", f->name, f->name);

    /* The 16-byte constant pool, after the body so the .text run stays
     * contiguous. `.p2align 4` is not needed by the load -- adrp/add
     * computes a whole address and `ldr q` tolerates any alignment on
     * normal memory -- but a 16-byte datum that straddles a cache line for
     * no reason is worth one directive. */
    if (f->ncpool) {
        buf_printf(out, e.apple ? "\t.section\t__TEXT,__const\n"
                                : "\t.section\t.rodata\n");
        buf_printf(out, "\t.p2align\t4\n");
        for (i = 0; i < f->ncpool; i++) {
            buf_printf(out, "%scp%u_%u:\n", mprivate(&e), fidx, i);
            buf_printf(out, "\t.quad\t%llu\n",
                       (unsigned long long)f->cpool[2 * i]);
            buf_printf(out, "\t.quad\t%llu\n",
                       (unsigned long long)f->cpool[2 * i + 1]);
        }
    }
}

/* --- data emission ----------------------------------------------------------
 *
 * Byte image plus relocation list, emitted NUMERICALLY: no .ascii dialects,
 * because Sprint 0's byte-for-byte determinism law applies to both backends
 * and escape spelling is exactly where assemblers disagree. Relocations are
 * eight-byte `.quad sym+addend` splices; zero runs of sixteen or more
 * collapse to `.zero`. Identical in shape to the x86 emitter on purpose. */
static void emit_image(Emit *e, const IrModule *m, const IrGlobal *g, Buf *out)
{
    u64 off = 0;
    u32 ri = 0;

    while (off < g->size) {
        if (ri < g->nrelocs && g->relocs[ri].offset == off) {
            const IrReloc *r = &g->relocs[ri++];

            buf_printf(out, "\t.quad\t%s", msym(e, m->syms[r->symbol]));
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

/* Mach-O file-scope bookends. `.subsections_via_symbols` is REQUIRED, not
 * cosmetic: ld64's atom model and its dead-strip both assume every function
 * and datum is its own subsection, which is what that directive asserts. */
void a64_emit_file_prologue(Buf *out)
{
    if (cgf_target_selected().kind != CGF_TARGET_ARM64_MACOS)
        return;
    buf_printf(out, "\t.build_version macos, 11, 0\n");
}

static void a64_emit_globals_filtered(const IrModule *m, Buf *out, bool tls,
                                      bool pic);

/* An inline-asm template, copied out VERBATIM between #APP/#NO_APP markers --
 * gcc's markers, which gas uses to relax its parsing and afs-as takes as
 * ordinary `#` comments. Copying unexamined is the contract: validating the
 * MNEMONICS here would mean maintaining a second assembler that disagrees
 * with the real one. ONE leading tab then the template verbatim, which is
 * what gcc does -- it indents only the first line. */
/* One operand, in the form the template asked for.
 *
 * ARM64 SPELLS ITS WIDTHS IN THE REGISTER NAME (`w0` against `x0`) where x86
 * puts them in the mnemonic suffix, so the width modifiers mean something
 * different here: gcc's arm64 `%w0`/`%x0` SELECT the name, and there is no
 * `%b`/`%k`. A bare `%0` gets the operand's own C width, which is what
 * `add %w0, %w1, #3` on ints relies on. */
static void a64_asm_operand(Buf *out, const IrModule *m, const A64AsmInfo *info,
                            u32 k, u8 sf_override)
{
    const IrAsm *a = &m->asms[info->asm_index - 1];
    const A64AsmOp *o;
    u8 sf;

    if (k >= info->nops) {
        buf_printf(out, "<asm-operand-%u-out-of-range>", k);
        return;
    }
    o = &info->ops[k];
    if (o->cls == ASM_CLS_IMM) {
        buf_printf(out, "#%lld", (long long)a->ops[k].imm);
        return;
    }
    if (!o->reg.id) {
        buf_printf(out, "<bad-asm-operand-%u>", k);
        return;
    }
    if (o->cls == ASM_CLS_MEM) {
        buf_printf(out, "[%s]", rn(o->reg, A64_SF64));
        return;
    }
    sf = sf_override ? sf_override : (u8)(o->size > 4 ? A64_SF64 : A64_SF32);
    buf_printf(out, "%s", rn(o->reg, sf));
}

/* An inline-asm template. Basic asm (no colon in the source construct) passes
 * `%` through untouched -- gcc's rule, decided at parse time. With operands
 * the escapes are `%%`, `%0..%9`, `%w0`/`%x0` for an explicit width, and
 * `%[name]`. An unknown escape passes through: the template belongs to the
 * programmer and the assembler is entitled to complain about it. */
void a64_emit_asm_text(Buf *out, const IrModule *m, u32 asm_index,
                       const A64AsmInfo *info)
{
    const IrAsm *a;
    const char *c;

    if (!asm_index || asm_index > m->nasms)
        return;
    a = &m->asms[asm_index - 1];
    if (a->is_basic || !info || !info->nops) {
        buf_printf(out, "#APP\n\t%s\n#NO_APP\n", a->tmpl);
        return;
    }
    buf_printf(out, "#APP\n\t");
    for (c = a->tmpl; *c; c++) {
        u8 sfo = 0;

        if (*c != '%') {
            buf_printf(out, "%c", *c);
            continue;
        }
        c++;
        if (*c == '%') {
            buf_printf(out, "%%");
            continue;
        }
        if ((*c == 'w' || *c == 'x') && c[1] &&
            ((c[1] >= '0' && c[1] <= '9') || c[1] == '[')) {
            sfo = (u8)(*c == 'w' ? A64_SF32 : A64_SF64);
            c++;
        }
        if (*c == '[') {
            const char *nm = c + 1;
            const char *end = nm;
            u32 j;

            while (*end && *end != ']')
                end++;
            for (j = 0; j < a->nops; j++)
                if (a->ops[j].name &&
                    strncmp(a->ops[j].name, nm, (size_t)(end - nm)) == 0 &&
                    a->ops[j].name[end - nm] == '\0')
                    break;
            if (j < a->nops)
                a64_asm_operand(out, m, info, j, sfo);
            else
                buf_printf(out, "<unknown-asm-operand>");
            c = *end ? end : end - 1;
            continue;
        }
        if (*c >= '0' && *c <= '9') {
            a64_asm_operand(out, m, info, (u32)(*c - '0'), sfo);
            continue;
        }
        buf_printf(out, "%%");
        if (sfo)
            buf_printf(out, "%c", sfo == A64_SF32 ? 'w' : 'x');
        if (*c)
            buf_printf(out, "%c", *c);
        else
            c--;
    }
    buf_printf(out, "\n#NO_APP\n");
}

/* The thread-locals, emitted BEFORE any function. See the filter's comment. */
void a64_emit_tls_decls(const IrModule *m, Buf *out)
{
    a64_emit_globals_filtered(m, out, true, false);
}

void a64_emit_file_epilogue(Buf *out)
{
    if (cgf_target_selected().kind != CGF_TARGET_ARM64_MACOS)
        return;
    buf_printf(out, "\t.subsections_via_symbols\n");
}

/* Globals, filtered by thread-locality. The two groups are emitted at
 * different POINTS in the file: gas tracks whether a symbol is thread-local
 * and rejects a TLS relocation against one it has not yet seen defined in a
 * TLS section ("Accessing `x' as thread-local object"). Functions come
 * before data here, so the thread-locals have to go first -- which is
 * exactly the order gcc emits them in. */
static void a64_emit_globals_filtered(const IrModule *m, Buf *out, bool tls,
                                      bool pic)
{
    Emit e;
    u32 i;

    memset(&e, 0, sizeof(e));
    e.out = out;
    e.m = m;
    e.apple = cgf_target_selected().kind == CGF_TARGET_ARM64_MACOS;

    for (i = 0; i < m->nglobals; i++) {
        const IrGlobal *g = &m->globals[i];
        u32 p2 = 0;
        u32 a;

        if (g->is_tls != tls)
            continue;

        for (a = g->align; a > 1; a >>= 1)
            p2++;
        if (g->is_tentative && !g->is_const) {
            /* A tentative definition is Mach-O's __common, reached through
             * .zerofill rather than .comm -- and it still needs its .globl,
             * which .comm implies on ELF. */
            if (e.apple) {
                buf_printf(out, "\t.globl\t%s\n", msym(&e, g->name));
                buf_printf(out, "\t.zerofill\t__DATA,__common,%s,%llu,%u\n",
                           msym(&e, g->name), (unsigned long long)g->size, p2);
            } else {
                buf_printf(out, "\t.comm\t%s,%llu,%u\n", g->name,
                           (unsigned long long)g->size, g->align);
            }
            continue;
        }
        if (g->linkage == IRLINK_INTERNAL) {
            /* Mach-O has no `.local`: a symbol is internal precisely by NOT
             * being declared .globl. */
            if (!e.apple)
                buf_printf(out, "\t.local\t%s\n", g->name);
        } else {
            buf_printf(out,
                       (!e.apple && g->is_weak) ? "\t.weak\t%s\n"
                                                : "\t.globl\t%s\n",
                       msym(&e, g->name));
        }
        a64_symbol_attrs(&e, out, g->name, g->visibility);
        /* Zero-initialized data is a DIRECTIVE on Mach-O, not a section plus
         * a run of zero bytes, and it carries its own name/size/alignment --
         * so it replaces the label and the body both. */
        if (e.apple && !g->init && !g->is_const) {
            buf_printf(out, "\t.zerofill\t__DATA,%s,%s,%llu,%u\n",
                       g->linkage == IRLINK_INTERNAL ? "__bss" : "__common",
                       msym(&e, g->name), (unsigned long long)g->size, p2);
            continue;
        }
        if (g->section) {
            if (e.apple)
                CGF_ICE("arm64-macos: section(\"%s\") needs the Mach-O "
                        "SEGMENT,SECTION spelling (SEC-MACHO-001)",
                        g->section);
            /* A named section forces PROGBITS even when the object has no
             * initializer: the bytes belong where the author put them. */
            buf_printf(out, "\t.section\t%s,\"aw\"\n", g->section);
        } else {
            /* Both dialects of the ONE shared rule. Mach-O spells .rodata
             * `__TEXT,__const` and .data.rel.ro `__DATA,__const`, which is
             * what clang emits for arm64-apple-macos. The T flag is what
             * marks an ELF section thread-local; the linker lays those out as
             * the TEMPLATE each new thread is given, and gas needs to have
             * seen the definition before it will accept a TLS relocation
             * naming the symbol. */
            switch (cg_global_segment(g, pic)) {
            case CG_SEG_TDATA:
                buf_printf(out, "\t.section\t.tdata,\"awT\",@progbits\n");
                break;
            case CG_SEG_TBSS:
                buf_printf(out, "\t.section\t.tbss,\"awT\",@nobits\n");
                break;
            case CG_SEG_RODATA:
                buf_printf(out, e.apple ? "\t.section\t__TEXT,__const\n"
                                        : "\t.section\t.rodata\n");
                break;
            case CG_SEG_DATA_REL_RO:
                buf_printf(out,
                           e.apple
                               ? "\t.section\t__DATA,__const\n"
                               : "\t.section\t.data.rel.ro,\"aw\",@progbits\n");
                break;
            case CG_SEG_BSS:
                buf_printf(out, e.apple ? "\t.section\t__DATA,__data\n"
                                        : "\t.section\t.bss\n");
                break;
            case CG_SEG_DATA:
                buf_printf(out, e.apple ? "\t.section\t__DATA,__data\n"
                                        : "\t.section\t.data\n");
                break;
            }
        }
        buf_printf(out, "\t.p2align\t%u\n", p2);
        if (!e.apple) {
            buf_printf(out, "\t.type\t%s, %s\n", g->name,
                       g->is_tls ? "@tls_object" : "@object");
            buf_printf(out, "\t.size\t%s, %llu\n", g->name,
                       (unsigned long long)g->size);
        }
        buf_printf(out, "%s:\n", msym(&e, g->name));
        if (!g->init)
            buf_printf(out, "\t.zero\t%llu\n", (unsigned long long)g->size);
        else
            emit_image(&e, m, g, out);
    }
}

/* `constructor`/`destructor` entries. `.xword` is the arm64 spelling of the
 * 8-byte absolute pointer x86 writes as `.quad`, and as there it is the same
 * under every PIC mode -- the link turns it into a RELATIVE relocation.
 *
 * Mach-O keeps constructors in `__DATA,__mod_init_func` and DESTRUCTORS
 * nowhere: clang synthesizes a wrapper function that calls
 * `__cxa_atexit(f, 0, &__dso_handle)` and registers THAT as an initializer.
 * That is real machinery rather than a dialect difference, so it is refused by
 * name instead of being emitted into a section the loader never reads -- a
 * destructor that silently never runs is the failure this file's tier rules
 * exist to prevent. Priorities on Mach-O order entries WITHIN this object
 * only, because ld64 does not sort the section; clang has the same limit. */
static void a64_emit_init_array(Emit *e, const IrModule *m, Buf *out)
{
    u32 i;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        bool want_ctor = pass == 0;

        for (i = 0; i < m->nfuncs; i++) {
            const IrFunc *f = &m->funcs[i];
            char sec[32];

            if (want_ctor ? !f->is_ctor : !f->is_dtor)
                continue;
            if (e->apple) {
                /* An unreachable backstop: lowering already refused this with
                 * a real diagnostic, which is what a user must see. */
                if (!want_ctor)
                    CGF_ICE("arm64-macos: destructor reached emission");
                buf_printf(
                    out, "\t.section\t__DATA,__mod_init_func,mod_init_funcs\n");
                buf_printf(out, "\t.p2align\t3\n");
                buf_printf(out, "\t.quad\t%s\n", msym(e, f->name));
                continue;
            }
            cg_init_array_section(sec, sizeof(sec), want_ctor,
                                  want_ctor ? f->ctor_prio : f->dtor_prio);
            buf_printf(out, "\t.section\t%s,\"aw\"\n", sec);
            buf_printf(out, "\t.p2align\t3\n");
            buf_printf(out, "\t.xword\t%s\n", msym(e, f->name));
        }
    }
}

void a64_emit_globals(const IrModule *m, Buf *out, bool pic)
{
    Emit e;
    u32 i;

    memset(&e, 0, sizeof(e));
    e.out = out;
    e.m = m;
    e.apple = cgf_target_selected().kind == CGF_TARGET_ARM64_MACOS;
    /* File-scope asm, verbatim and first: it routinely DEFINES symbols the
     * rest of the file references, and emitting it before any section switch
     * leaves the template in control of its own section. */
    for (i = 0; i < m->nfile_asms; i++) {
        buf_printf(out, "#APP\n\t%s\n#NO_APP\n", m->file_asms[i]);
    }
    /* Undefined ELF symbols have no IrGlobal/IrFunc record to carry GNU
     * attributes.  Emit their declarations explicitly; Mach-O needs the
     * distinct `.weak_reference` model and rejects these attributes earlier. */
    if (!e.apple) {
        for (i = 0; i < m->nsyms; i++) {
            IrSymBinding binding = ir_sym_binding(m, i + 1);
            const IrSymAttrs *attrs = &m->sym_attrs[i];
            const char *name = msym(&e, ir_sym_asm_spelling(m->syms[i]));

            if (binding.defined_here)
                continue;
            if (attrs->is_weak)
                buf_printf(out, "\t.weak\t%s\n", name);
            a64_symbol_attrs(&e, out, name, attrs->visibility);
        }
    }
    /* Aliases need no section and no alignment, so they precede the data.
     * Mach-O spells the symbol with a leading underscore exactly as every
     * other symbol does; `.set` itself is the same directive on both. */
    for (i = 0; i < m->naliases; i++) {
        const IrAlias *a = &m->aliases[i];

        if (a->linkage != IRLINK_INTERNAL) {
            /* `.weak_definition` is MACH-O's spelling and ELF's assembler
             * rejects it outright; ELF wants `.weak`, exactly as x86 emits.
             * Copying the Apple form into the ELF path is why this lane exists
             * at all -- the assembler caught it on the first real link. */
            if (a->is_weak)
                buf_printf(
                    out, e.apple ? "\t.weak_definition\t%s\n" : "\t.weak\t%s\n",
                    msym(&e, a->name));
            else
                buf_printf(out, "\t.globl\t%s\n", msym(&e, a->name));
        }
        a64_symbol_attrs(&e, out, a->name, a->visibility);
        buf_printf(out, "\t.set\t%s,%s\n", msym(&e, a->name),
                   msym(&e, a->target));
    }
    a64_emit_globals_filtered(m, out, false, pic);
    a64_emit_init_array(&e, m, out);
}
