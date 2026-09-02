#include <stdarg.h>
#include <string.h>

#include "lower/lower.h"

/* Inline-asm lowering (Sprint 55 D2): C constraints -> IrAsm.
 *
 * THE CONSTRAINT LETTERS ARE TARGET VOCABULARY, which is why this decoding
 * lives here rather than in sema. `r` means the same thing everywhere, but
 * `d` is rdx on x86-64 and a d-register on arm64, and `x` is an xmm register
 * on x86-64 and nothing at all on arm64. The backend is handed a CLASS and a
 * physical register number; it never sees a letter.
 *
 * OUTPUTS TRAVEL AS ADDRESSES. C asm can have several outputs and an IR
 * instruction defines at most one value, so `"=r"(o)` passes `&o` and the
 * backend stores the allocated register through it after the template runs.
 * The constraint is still honoured -- the value really is in a register while
 * the template executes -- and the model needs no tuple type. See ir.h. */

typedef struct {
    char letter;
    u8 reg; /* target register number */
} FixedReg;

/* One reporting site, matching lower_unimplemented's contract: report once
 * and latch, so a malformed asm does not produce a diagnostic per operand
 * and the driver still turns the latch into a nonzero exit. */
static void asm_error(Lower *lo, Span span, const char *fmt, ...)
{
    va_list ap;

    if (!lo->failed) {
        va_start(ap, fmt);
        diag_emit_warn_v(lo->dc, DIAG_ERROR, span, WARN_NONE, fmt, ap);
        va_end(ap);
    }
    lo->failed = true;
}

static bool asm_target_is_arm64(void)
{
    TargetKind k = cgf_target_selected().kind;

    return k == CGF_TARGET_ARM64_LINUX || k == CGF_TARGET_ARM64_MACOS;
}

/* SysV x86-64 register numbers as the backend spells them (X64Reg). gcc's
 * letters, and the four that matter for musl are `a`, `c`, `d`, `S`, `D`:
 * every syscall wrapper names them directly. */
static const FixedReg x86_fixed[] = {
    {'a', 0}, /* rax */
    {'b', 3}, /* rbx */
    {'c', 1}, /* rcx */
    {'d', 2}, /* rdx */
    {'S', 6}, /* rsi */
    {'D', 7}, /* rdi */
};

static bool fixed_for(TargetKind target, char c, u8 *out)
{
    u32 i;

    if (target == CGF_TARGET_ARM64_LINUX || target == CGF_TARGET_ARM64_MACOS)
        return false; /* AAPCS64 has no single-letter register constraints */
    for (i = 0; i < sizeof(x86_fixed) / sizeof(x86_fixed[0]); i++)
        if (x86_fixed[i].letter == c) {
            *out = x86_fixed[i].reg;
            return true;
        }
    return false;
}

/* A named register clobber, `"rax"` or `"x5"` style. The names are the ones a
 * programmer writes in the template, which are the ASSEMBLER's spellings and
 * not necessarily the backend's -- x86 accepts both `rax` and `eax` for the
 * same register, and gcc treats them as the same clobber. */
bool lower_asm_clobber_reg(const char *name, u8 *out)
{
    static const struct {
        const char *n;
        u8 reg;
    } x86[] = {
        {"rax", 0},  {"eax", 0},  {"ax", 0},   {"al", 0},   {"rcx", 1},
        {"ecx", 1},  {"cx", 1},   {"cl", 1},   {"rdx", 2},  {"edx", 2},
        {"dx", 2},   {"dl", 2},   {"rbx", 3},  {"ebx", 3},  {"bx", 3},
        {"bl", 3},   {"rsi", 6},  {"esi", 6},  {"si", 6},   {"rdi", 7},
        {"edi", 7},  {"di", 7},   {"r8", 8},   {"r9", 9},   {"r10", 10},
        {"r11", 11}, {"r12", 12}, {"r13", 13}, {"r14", 14}, {"r15", 15},
    };
    u32 i;

    if (asm_target_is_arm64()) {
        /* x0-x30 / w0-w30 name the same register in two widths. */
        u32 v = 0;
        const char *p = name;

        if (*p != 'x' && *p != 'w')
            return false;
        for (p++; *p; p++) {
            if (*p < '0' || *p > '9')
                return false;
            v = v * 10 + (u32)(*p - '0');
        }
        if (p == name + 1 || v > 30)
            return false;
        *out = (u8)v;
        return true;
    }
    for (i = 0; i < sizeof(x86) / sizeof(x86[0]); i++)
        if (strcmp(x86[i].n, name) == 0) {
            *out = x86[i].reg;
            return true;
        }
    return false;
}

/* Decode one constraint string into an IrAsmOp. Modifiers come first in gcc's
 * grammar and any order is accepted; the CLASS letter is whichever of the
 * remaining characters we recognise. Unknown letters are an error rather than
 * a silent "treat it as r": a constraint we do not model is a constraint the
 * template is relying on, and guessing produces wrong code that assembles. */
static bool decode_constraint(Lower *lo, IrAsmOp *op, const char *c,
                              bool is_output, Span span)
{
    bool saw_class = false;

    op->cls = ASM_CLS_REG;
    op->tied_to = -1;
    for (; *c; c++) {
        switch (*c) {
        case '=':
            /* Write-only. The parser already knows outputs by position. */
            continue;
        case '+':
            /* Read-write. Desugared by the caller into this output plus a
             * matching input, so by here it means only "an output". */
            continue;
        case '&':
            op->early_clobber = true;
            continue;
        case '%':
            /* "commutative with the next operand" -- an allocation HINT,
             * and ignoring a hint cannot make a correct program wrong. */
            continue;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            if (is_output) {
                asm_error(lo, span,
                          "a matching constraint '%c' names an output, so "
                          "it cannot appear on one",
                          *c);
                return false;
            }
            op->tied_to = *c - '0';
            saw_class = true;
            continue;
        case 'r':
            op->cls = ASM_CLS_REG;
            saw_class = true;
            continue;
        case 'm':
            op->cls = ASM_CLS_MEM;
            saw_class = true;
            continue;
        case 'i':
        case 'n':
            op->cls = ASM_CLS_IMM;
            saw_class = true;
            continue;
        case 'N':
            /* x86 N is an unsigned 8-bit immediate. Keep the exact spelling
             * narrow here: a string such as rN is an ALTERNATIVE constraint,
             * and choosing between its classes needs the same expression-
             * aware path as Nd below rather than "last letter wins". */
            if (asm_target_is_arm64() || strcmp(op->constraint, "N") != 0)
                break;
            op->cls = ASM_CLS_IMM;
            saw_class = true;
            continue;
        case 'g':
            /* r, m or i. We take the register reading, which is always a
             * legal choice for `g` and is what gcc picks for a value in a
             * register anyway. */
            op->cls = ASM_CLS_REG;
            saw_class = true;
            continue;
        case 'x':
            if (asm_target_is_arm64())
                break; /* not an arm64 letter */
            op->cls = ASM_CLS_FPREG;
            saw_class = true;
            continue;
        case 't':
            if (asm_target_is_arm64())
                break; /* x87 stack top exists only on x86 */
            op->cls = ASM_CLS_X87;
            saw_class = true;
            continue;
        case 'u':
            if (asm_target_is_arm64())
                break; /* x87 second stack slot exists only on x86 */
            op->cls = ASM_CLS_X87UP;
            saw_class = true;
            continue;
        case 'X':
            /* gcc's "any operand" class. A GP register is a legal choice
             * for the scalar/pointer uses represented by this IR; musl uses
             * it in an empty address-escape asm. */
            op->cls = ASM_CLS_REG;
            saw_class = true;
            continue;
        case 'w':
            if (!asm_target_is_arm64())
                break; /* not an x86 letter */
            op->cls = ASM_CLS_FPREG;
            saw_class = true;
            continue;
        case 'q':
        case 'Q':
            /* Byte-addressable GP. Every allocatable GP register on x86-64
             * has a byte form, so this is `r` here -- unlike 32-bit x86,
             * where it genuinely excludes esi/edi. */
            if (asm_target_is_arm64())
                break;
            op->cls = ASM_CLS_REG;
            saw_class = true;
            continue;
        default:
            break;
        }
        {
            u8 reg;

            if (fixed_for(cgf_target_selected().kind, *c, &reg)) {
                op->cls = ASM_CLS_FIXED;
                op->reg = reg;
                saw_class = true;
                continue;
            }
        }
        asm_error(lo, span,
                  "unsupported asm constraint character '%c' in \"%s\"; "
                  "an unmodelled constraint would assemble and then run "
                  "with the operand in the wrong place",
                  *c, op->constraint);
        return false;
    }
    if (!saw_class) {
        asm_error(lo, span, "asm constraint \"%s\" names no operand class",
                  op->constraint);
        return false;
    }
    return true;
}

/* True when the constraint string carries `+`: gcc's read-write output, which
 * is exactly an output plus an input tied to it. Desugaring here rather than
 * in the backend is what keeps "two operands, one location" a single concept
 * the allocator already understands from matching constraints. */
static bool constraint_has(const char *c, char want)
{
    for (; *c; c++)
        if (*c == want)
            return true;
    return false;
}

static bool local_register_supported(u8 reg)
{
    if (!asm_target_is_arm64())
        return true;

    /* Keep local-register bindings inside the allocator's ordinary GP
     * register set. x12/x13 belong to atomic and late-frame expansions,
     * x14-x17 are spill/reload scratches, x18 is the platform register, and
     * x29/x30 are the frame/link registers. Pre-colouring an asm operand to
     * any of those would let it collide with backend-owned state even though
     * the general asm-clobber vocabulary must continue to recognize their
     * names. */
    return reg <= 11 || (reg >= 19 && reg <= 28);
}

/* GNU's local-register-variable promise is much narrower than "keep this C
 * object in one physical register for its whole lifetime": the binding is
 * guaranteed when the variable itself is a direct extended-asm operand. That
 * is exactly the boundary represented here. An explicit fixed constraint
 * still wins (`"a"(r10)` asks GCC to copy r10 into rax); only a generic GP
 * register class inherits the declaration's binding. */
static bool bind_local_register_operand(Lower *lo, IrAsmOp *op,
                                        const AsmOperand *src)
{
    AstNode *e = src->expr;
    Symbol *sym;
    u8 reg;

    while (e && e->kind == AST_EXPR_PAREN)
        e = e->lhs;
    if (!e || e->kind != AST_EXPR_IDENT || !e->sym ||
        !e->sym->asm_register_name || op->cls != ASM_CLS_REG ||
        op->tied_to >= 0)
        return true;
    sym = e->sym;
    if (!lower_asm_clobber_reg(sym->asm_register_name, &reg)) {
        asm_error(lo, src->span,
                  "unsupported register name '%s' on local register "
                  "variable '%s' for this target",
                  sym->asm_register_name, sym->name);
        return false;
    }
    if (!local_register_supported(reg)) {
        asm_error(lo, src->span,
                  "register name '%s' on local register variable '%s' is "
                  "reserved by the target backend",
                  sym->asm_register_name, sym->name);
        return false;
    }
    op->cls = ASM_CLS_FIXED;
    op->reg = reg;
    return true;
}

static u32 tied_input_count(const IrAsmOp *ops, u32 n, u32 output)
{
    u32 count = 0;
    u32 i;

    for (i = 0; i < n; i++)
        if (!ops[i].is_output && ops[i].tied_to == (i32)output)
            count++;
    return count;
}

/* The x87 stack is deliberately not part of either backend register bank.
 * Accept only shapes whose depth can be established around the opaque
 * template; x86 isel then emits balanced fld/template/fstp sequences without
 * exposing an x87 value to SSA or register allocation. */
static bool validate_x87_shape(Lower *lo, const AstNode *s, const IrAsmOp *ops,
                               u32 n)
{
    u32 i;
    u32 nx87 = 0;
    bool clobbers_st = false;

    for (i = 0; i < n; i++)
        if (ops[i].cls == ASM_CLS_X87 || ops[i].cls == ASM_CLS_X87UP)
            nx87++;
    if (!nx87)
        return true;
    for (i = 0; i < s->asm_nclobbers; i++)
        if (strcmp(s->asm_clobbers[i], "st") == 0)
            clobbers_st = true;
    /* One st(0) input consumed by the template into a memory output:
     * musl's fistpll conversion form. The explicit clobber proves the pop. */
    if (s->asm_noutputs == 1 && n == 2 && ops[0].is_output &&
        ops[0].cls == ASM_CLS_MEM && !ops[1].is_output &&
        ops[1].cls == ASM_CLS_X87 && clobbers_st)
        return true;
    /* st(0) result, st(1) divisor, and one fixed status-word result:
     * musl's fprem/fprem1 loop. The `+t` tied input is appended last. */
    if (s->asm_noutputs == 2 && n == 4 && ops[0].is_output &&
        ops[0].cls == ASM_CLS_X87 && constraint_has(ops[0].constraint, '+') &&
        ops[1].is_output && ops[1].cls == ASM_CLS_FIXED && ops[1].reg == 0 &&
        !ops[2].is_output && ops[2].cls == ASM_CLS_X87UP && !ops[3].is_output &&
        ops[3].cls == ASM_CLS_X87 && ops[3].tied_to == 0)
        return true;
    /* One floating value tied to st(0), either GNU's read-write `+t`
     * spelling or its equivalent write-only `=t` output plus a matching
     * `0` input.  fld/fstp carry the C width, so f32/f64 values make the
     * same locally balanced stack shape as an f80 long double. */
    if (s->asm_noutputs == 1 && n == 2 && ops[0].is_output &&
        ops[0].cls == ASM_CLS_X87 && !ops[1].is_output &&
        ops[1].cls == ASM_CLS_X87 && ops[1].tied_to == 0 &&
        s->asm_ops[0].expr && s->asm_ops[0].expr->sem_type) {
        IrType out_type = lower_irtype(lo, s->asm_ops[0].expr->sem_type);
        IrType in_type = out_type;

        if (!constraint_has(ops[0].constraint, '+')) {
            if (s->asm_nops < 2 || !s->asm_ops[1].expr ||
                !s->asm_ops[1].expr->sem_type)
                goto unsupported;
            in_type = lower_irtype(lo, s->asm_ops[1].expr->sem_type);
        }
        if (in_type == out_type &&
            (out_type == IRT_F32 || out_type == IRT_F64 || out_type == IRT_F80))
            return true;
    }
unsupported:
    asm_error(lo, s->span,
              "unsupported x87 asm operand shape; supported forms are one "
              "tied floating st(0) value (\"+t\" or \"=t\"/\"0\"), "
              "musl's t/u remainder loop, and a clobbered t input converted "
              "to memory");
    return false;
}

/* The shared MIR view still has one def, but x86 can soundly carry a narrow
 * second-output shape without changing it: a FIXED output with exactly one
 * matching input has that input reserve the physical register at the asm,
 * then the backend READREG-captures the new value immediately afterwards.
 * This is glibc <sys/io.h>'s =D/=c + 0/1 shape. An unfixed extra output has
 * nowhere to record the allocator's choice, and an unmatched fixed output
 * has nothing keeping its register occupied while the template runs. */
static bool validate_register_outputs(Lower *lo, const AstNode *s,
                                      const IrAsmOp *ops, u32 n)
{
    u32 register_outputs = 0;
    u32 i, j;

    for (i = 0; i < s->asm_noutputs && i < n; i++) {
        const IrAsmOp *op = &ops[i];

        if (op->cls == ASM_CLS_MEM || op->cls == ASM_CLS_X87 ||
            op->cls == ASM_CLS_X87UP)
            continue;
        register_outputs++;
        if (op->cls == ASM_CLS_FIXED)
            for (j = 0; j < i; j++)
                if (ops[j].is_output && ops[j].cls == ASM_CLS_FIXED &&
                    ops[j].reg == op->reg) {
                    asm_error(lo, s->asm_ops[i].span,
                              "asm outputs %u and %u both require the same "
                              "fixed register; two distinct outputs cannot "
                              "occupy one location",
                              (unsigned)j, (unsigned)i);
                    return false;
                }
        if (register_outputs == 1)
            continue;
        if (asm_target_is_arm64()) {
            asm_error(lo, s->asm_ops[i].span,
                      "an asm with more than one register output is not "
                      "supported on arm64 yet");
            return false;
        }
        if (op->cls != ASM_CLS_FIXED || tied_input_count(ops, n, i) != 1) {
            asm_error(lo, s->asm_ops[i].span,
                      "an asm with more than one register output is not "
                      "supported yet unless every extra output names one "
                      "fixed x86 register and has exactly one matching "
                      "input; a general extra output would require another "
                      "MIR def (docs/gnu-extensions.md)");
            return false;
        }
    }
    return true;
}

static bool reg_in_list(const u8 *regs, u32 n, u8 reg)
{
    u32 i;

    for (i = 0; i < n; i++)
        if (regs[i] == reg)
            return true;
    return false;
}

static bool validate_operand_clobbers(Lower *lo, const AstNode *s,
                                      const IrAsmOp *ops, u32 n,
                                      const u8 *clobregs, u32 nclob)
{
    u32 i;

    for (i = 0; i < n; i++) {
        const IrAsmOp *op = &ops[i];
        const IrAsmOp *fixed = op;
        Span span;

        if (op->tied_to >= 0 && (u32)op->tied_to < n)
            fixed = &ops[op->tied_to];
        if (fixed->cls != ASM_CLS_FIXED ||
            !reg_in_list(clobregs, nclob, fixed->reg))
            continue;
        span = i < s->asm_nops ? s->asm_ops[i].span
                               : s->asm_ops[(u32)op->tied_to].span;
        asm_error(lo, span,
                  "asm operand %u requires register %u, which is also named "
                  "in the clobber list",
                  (unsigned)i, (unsigned)fixed->reg);
        return false;
    }
    return true;
}

void lower_asm(Lower *lo, AstNode *s)
{
    IrAsm a;
    IrAsmOp ops[64];
    IrOperand vals[64];
    Lvalue output_lvalues[64];
    bool output_lvalue_valid[64] = {false};
    DeferredAsmImmediate *deferred[64];
    u8 clobregs[64];
    u32 n = 0;
    u32 nclob = 0;
    u32 ndeferred = 0;
    u32 i;

    memset(&a, 0, sizeof(a));
    a.tmpl = s->asm_tmpl ? s->asm_tmpl : "";
    a.is_basic = s->asm_basic;
    /* gcc's rule exactly: an asm with NO OUTPUTS is implicitly volatile,
     * because the only reason to write one is its side effects. Basic asm is
     * always in that position. */
    a.is_volatile = s->asm_volatile || s->asm_noutputs == 0;

    for (i = 0; i < s->asm_nops && n < 64; i++) {
        const AsmOperand *src = &s->asm_ops[i];
        bool is_out = i < s->asm_noutputs;
        bool nd_immediate = false;
        IrAsmOp *op = &ops[n];

        memset(op, 0, sizeof(*op));
        op->constraint = src->constraint ? src->constraint : "";
        op->name = src->name;
        op->is_output = is_out;
        /* glibc spells x86 "Nd" while musl spells the equivalent "dN":
         * choose N for an unsigned 8-bit constant and d (%rdx) otherwise.
         * Alternative order does not change the accepted locations. This is
         * not two cumulative class letters. CE_FOLD is deliberately silent:
         * a variable is not an error because the d arm accepts it. */
        if (!asm_target_is_arm64() && !is_out &&
            (strcmp(op->constraint, "Nd") == 0 ||
             strcmp(op->constraint, "dN") == 0)) {
            ConstValue cv = constexpr_eval(lo->sema, src->expr, CE_FOLD);

            op->tied_to = -1;
            if (cv.kind == CV_INT && cv.i <= 255) {
                op->cls = ASM_CLS_IMM;
                op->imm = (i64)cv.i;
                nd_immediate = true;
            } else {
                op->cls = ASM_CLS_FIXED;
                op->reg = 2; /* rdx */
            }
        } else if (!decode_constraint(lo, op, op->constraint, is_out,
                                      src->span)) {
            return;
        }
        /* A numeric matching constraint inherits its output's physical
         * location.  Register ties already consult the output explicitly in
         * each backend; x87 must also inherit the class here because st(0)
         * is stack-modelled rather than allocator-visible. */
        if (op->tied_to >= 0 && (u32)op->tied_to < n &&
            ops[op->tied_to].is_output &&
            (ops[op->tied_to].cls == ASM_CLS_X87 ||
             ops[op->tied_to].cls == ASM_CLS_X87UP)) {
            op->cls = ops[op->tied_to].cls;
            op->reg = ops[op->tied_to].reg;
        }
        if (!bind_local_register_operand(lo, op, src))
            return;
        /* Constraint decoding lives here because its vocabulary is target
         * specific.  Once `m` has selected the memory class, its C operand
         * must designate an object; an rvalue such as `sizeof x` has no
         * address for the backend to pass.  Diagnose at this boundary rather
         * than sending an rvalue into lower_lvalue(). */
        if (op->cls == ASM_CLS_MEM && (!src->expr || !src->expr->is_lvalue)) {
            asm_error(lo, src->span,
                      "an asm memory operand with constraint \"%s\" must be "
                      "an lvalue",
                      op->constraint);
            return;
        }
        if (src->expr && src->expr->sem_type) {
            TypeLayout l = layout_of(lo->sema, src->expr->sem_type);

            if (op->cls == ASM_CLS_X87 || op->cls == ASM_CLS_X87UP)
                op->size = (u8)(l.size > 16 ? 16 : (l.size ? l.size : 1));
            else
                op->size = (u8)(l.size > 8 ? 8 : (l.size ? l.size : 1));
        } else {
            op->size = 8;
        }
        if (op->cls == ASM_CLS_IMM) {
            /* `i` and `n` require an assemble-time constant.  The source
             * arm can, however, be removed by __builtin_constant_p before
             * code generation; diagnosing here would reject a constraint
             * that never reaches an asm instruction.  Carry a harmless
             * placeholder through lowering, then validate it after the CFG
             * has its real constant edge.  CE_FOLD is deliberately silent;
             * the deferred CE_ICE call owns the user diagnostic. */
            ConstValue cv;
            DeferredAsmImmediate *pending;

            if (nd_immediate) {
                vals[n] = ir_op_iconst(IRT_I64, op->imm);
                n++;
                continue;
            }
            cv = constexpr_eval(lo->sema, src->expr, CE_FOLD);
            op->imm = cv.kind == CV_INT ? (i64)cv.i : 0;
            vals[n] = ir_op_iconst(IRT_I64, op->imm);
            pending = arena_alloc(lo->arena, sizeof(*pending),
                                  _Alignof(DeferredAsmImmediate));
            pending->expr = src->expr;
            pending->constraint = op->constraint;
            pending->span = src->span;
            pending->block = BLOCK_INVALID;
            pending->next = NULL;
            if (lo->deferred_asm_immediates_tail)
                lo->deferred_asm_immediates_tail->next = pending;
            else
                lo->deferred_asm_immediates = pending;
            lo->deferred_asm_immediates_tail = pending;
            deferred[ndeferred++] = pending;
            n++;
            continue;
        }
        if (is_out) {
            /* The ADDRESS of the output object; the backend stores through
             * it once the template has run. */
            Lvalue lv = lower_lvalue(lo, src->expr);

            vals[n] = lv.addr;
            output_lvalues[i] = lv;
            output_lvalue_valid[i] = true;
        } else if (op->cls == ASM_CLS_MEM) {
            Lvalue lv = lower_lvalue(lo, src->expr);

            vals[n] = lv.addr;
        } else {
            vals[n] = lower_rvalue(lo, src->expr);
        }
        n++;
    }
    a.noutputs = s->asm_noutputs;

    /* `+` desugars AFTER the ordinary pass so the added inputs land at the
     * end, where gcc puts them: the template's %N numbering counts the
     * declared operands only, and a tied input is never referenced by
     * number. */
    for (i = 0; i < s->asm_noutputs && n < 64; i++) {
        if (!constraint_has(ops[i].constraint, '+'))
            continue;
        memset(&ops[n], 0, sizeof(ops[n]));
        ops[n].constraint = ops[i].constraint;
        ops[n].is_output = false;
        ops[n].cls = ops[i].cls;
        ops[n].reg = ops[i].reg;
        ops[n].size = ops[i].size;
        ops[n].tied_to = (i32)i;
        /* Its VALUE, not its address: the tied input reads the object.  Reuse
         * the output's lvalue descriptor so an address expression with side
         * effects is evaluated only once, while volatile/atomic/bitfield
         * access properties still reach the load. */
        if (!output_lvalue_valid[i]) {
            asm_error(lo, s->asm_ops[i].span,
                      "a read-write asm output cannot use an immediate "
                      "constraint");
            return;
        }
        vals[n] = lower_load(lo, output_lvalues[i]);
        n++;
    }

    if (!validate_x87_shape(lo, s, ops, n) ||
        !validate_register_outputs(lo, s, ops, n))
        return;

    for (i = 0; i < s->asm_nclobbers; i++) {
        const char *c = s->asm_clobbers[i];
        u8 reg;

        if (strcmp(c, "memory") == 0) {
            a.clobbers_memory = true;
        } else if (strcmp(c, "cc") == 0) {
            a.clobbers_cc = true;
        } else if (!asm_target_is_arm64() && strcmp(c, "st") == 0) {
            /* x87 is stack-modelled around IR_ASM, never allocated. The
             * exact input-consuming form was checked above. */
            continue;
        } else if (lower_asm_clobber_reg(c, &reg)) {
            if (nclob < 64 && !reg_in_list(clobregs, nclob, reg))
                clobregs[nclob++] = reg;
        } else {
            asm_error(lo, s->span,
                      "unknown asm clobber \"%s\"; an unrecognised register "
                      "name would leave the allocator free to keep a value "
                      "in a register the template destroys",
                      c);
            return;
        }
    }

    if (!validate_operand_clobbers(lo, s, ops, n, clobregs, nclob))
        return;

    if (n) {
        a.ops = arena_alloc(lo->arena, n * sizeof(IrAsmOp), _Alignof(IrAsmOp));
        memcpy(a.ops, ops, n * sizeof(IrAsmOp));
    }
    a.nops = n;
    if (nclob) {
        a.clobber_regs = arena_alloc(lo->arena, nclob, 1);
        memcpy(a.clobber_regs, clobregs, nclob);
        a.nclobber_regs = nclob;
    }
    /* A later operand may lower a conditional expression and move the asm to
     * its join block, so attach every delayed immediate check only here. */
    for (i = 0; i < ndeferred; i++)
        deferred[i]->block = lo->b.block;
    ir_build_asm(&lo->b, ir_asm_new(lo->m, &a), vals, n);
}

void lower_asm_validate_deferred_immediates(Lower *lo)
{
    DeferredAsmImmediate *pending;

    for (pending = lo->deferred_asm_immediates; pending && !lo->failed;
         pending = pending->next) {
        ConstValue cv;

        if (!ir_func_block_reachable(lo->fn, pending->block))
            continue;
        cv = constexpr_eval(lo->sema, pending->expr, CE_ICE);
        if (cv.kind != CV_INT) {
            asm_error(lo, pending->span,
                      "an asm operand with constraint \"%s\" must be an "
                      "integer constant expression",
                      pending->constraint);
            return;
        }
        if (strcmp(pending->constraint, "N") == 0 && cv.i > 255) {
            asm_error(lo, pending->span,
                      "an asm operand with constraint \"N\" must be an "
                      "unsigned 8-bit integer constant");
            return;
        }
    }
}
