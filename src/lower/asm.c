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

void lower_asm(Lower *lo, AstNode *s)
{
    IrAsm a;
    IrAsmOp ops[64];
    IrOperand vals[64];
    u8 clobregs[64];
    u32 n = 0;
    u32 nclob = 0;
    u32 i;
    bool seen_reg_output = false;

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
        IrAsmOp *op = &ops[n];

        memset(op, 0, sizeof(*op));
        op->constraint = src->constraint ? src->constraint : "";
        op->name = src->name;
        op->is_output = is_out;
        if (!decode_constraint(lo, op, op->constraint, is_out, src->span))
            return;
        if (src->expr && src->expr->sem_type) {
            TypeLayout l = layout_of(lo->sema, src->expr->sem_type);

            op->size = (u8)(l.size > 8 ? 8 : (l.size ? l.size : 1));
        } else {
            op->size = 8;
        }
        /* ONE REGISTER OUTPUT, and the boundary is measured rather than
         * guessed. An IR instruction defines at most one value and the
         * shared MIR view reports one def per instruction, so a second
         * register output would mean widening that interface across both
         * backends. Counting musl's asm sites for OUR two targets says the
         * second output is almost always a MEMORY one (`"=m"`, `"=Q"` in
         * the atomics), which consumes no register at all: x86_64 has 181
         * one-output sites and 27 two-output, and aarch64 172 and 19, with
         * the two-output cases dominated by that shape. So one register
         * output plus any number of memory outputs covers the campaign,
         * and a second REGISTER output is refused by name. */
        if (is_out && op->cls != ASM_CLS_MEM) {
            if (seen_reg_output) {
                asm_error(lo, src->span,
                          "an asm with more than one register output is not "
                          "supported yet: an IR instruction defines one "
                          "value, so a second would need the shared MIR "
                          "view widened. A memory output (\"=m\") is "
                          "unaffected (docs/gnu-extensions.md)");
                return;
            }
            seen_reg_output = true;
        }
        if (op->cls == ASM_CLS_IMM) {
            /* `i` and `n` require an assemble-time constant, so the operand
             * must FOLD -- and a diagnostic beats emitting `$0`, which is
             * what an unpopulated field silently produced in the first
             * draft: `"i"(100)` added zero and the fixture read 2 for 102. */
            ConstValue cv = constexpr_eval(lo->sema, src->expr, CE_ICE);

            if (cv.kind != CV_INT) {
                asm_error(lo, src->span,
                          "an asm operand with constraint \"%s\" must be an "
                          "integer constant expression",
                          op->constraint);
                return;
            }
            op->imm = (i64)cv.i;
            vals[n] = ir_op_iconst(IRT_I64, (i64)cv.i);
            n++;
            continue;
        }
        if (is_out) {
            /* The ADDRESS of the output object; the backend stores through
             * it once the template has run. */
            Lvalue lv = lower_lvalue(lo, src->expr);

            vals[n] = lv.addr;
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
        ops[n].tied_to = (i32)i;
        /* Its VALUE, not its address: the tied input reads the object. */
        vals[n] = lower_rvalue(lo, s->asm_ops[i].expr);
        n++;
    }

    for (i = 0; i < s->asm_nclobbers; i++) {
        const char *c = s->asm_clobbers[i];
        u8 reg;

        if (strcmp(c, "memory") == 0) {
            a.clobbers_memory = true;
        } else if (strcmp(c, "cc") == 0) {
            a.clobbers_cc = true;
        } else if (lower_asm_clobber_reg(c, &reg)) {
            if (nclob < 64)
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
    ir_build_asm(&lo->b, ir_asm_new(lo->m, &a), vals, n);
}
