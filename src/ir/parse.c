#include "ir/ir.h"

#include <string.h>

#include "util/strmap.h"

/* The .cgfir parser — print.c's inverse; the grammar comment there is the
 * shared spec. Diagnostic-quality by design (file:line:col spans through
 * the Sprint 0 DiagCtx, first error wins and parsing stops): Sprint 20
 * fuzzes this front door, and hand-written pass tests live behind it for
 * the rest of the compiler's life, so "assert-fest" was never an option.
 *
 * Shape: the whole file is lexed into one arena token array up front
 * (trivial multi-pass + lookahead), then
 *   pre-scan 1: function names at brace depth 0 -> FUNCREF_INTERNAL ids
 *   pre-scan 2 (per function): block labels in layout order, so branches
 *     can target blocks defined later in the text
 *   parse: values resolve through a per-function name map; a %name used
 *     before its definition parks a FIXUP pointing at the final arena
 *     operand slot and resolves when the function closes. Forward value
 *     refs are LEGAL SSA (a dominating block may appear later in layout
 *     order), so this is required, not a convenience.
 *
 * The parser never ICEs on bad input and never enforces semantic rules —
 * append-after-terminator, arity mismatches, bad dominance all parse fine
 * and are the VERIFIER's to reject; the caller decides severity. Parser
 * errors are purely lexical/structural. */

typedef enum TokKind {
    T_EOF,
    T_IDENT,   /* keyword, op, type, label, init blob */
    T_PIDENT,  /* %name */
    T_AIDENT,  /* @name */
    T_XAIDENT, /* @!name: exact assembler spelling */
    T_INT,     /* [+-]?digits, value in ival as i64 bits */
    T_HEX,     /* 0x..., value in ival */
    /* "..." — a quoted byte string. A section name is the only user today,
     * and it needs one: `.note.GNU-stack` does not lex as an identifier here
     * (no leading '.', no '-'), and a section name is an arbitrary string
     * rather than a name in any of this format's namespaces. Only \" and \\
     * are escapes, the same two ISO gives _Pragma's destringize. */
    T_STR,
    T_LP,
    T_RP,
    T_LB,
    T_RB,
    T_COMMA,
    T_COLON,
    T_EQ,
    T_ELLIPSIS, /* '...' — variadic marker in func headers */
} TokKind;

typedef struct Tok {
    u8 kind;
    u32 line;
    u32 col;
    u32 len;
    const char *s; /* start in source; NOT nul-terminated */
    u64 ival;
} Tok;

typedef struct Fixup {
    IrOperand *slot;
    const char *name; /* arena copy */
    Tok tok;
} Fixup;

typedef struct P {
    Arena *arena;
    DiagCtx *dc;
    u32 file_id;
    Tok *toks;
    u32 ntoks;
    u32 pos;
    IrModule *m;
    /* module-level */
    Strmap func_ids; /* name -> arena-owned u32 (func index + 1) */
    /* per-function */
    IrFunc *f;
    BlockId cur_block;
    Strmap vals;   /* name -> arena-owned u32 ValueId */
    Strmap blocks; /* name -> arena-owned u32 BlockId */
    Fixup *fixups;
    u32 nfixups;
    u32 cap_fixups;
    bool failed;
} P;

static Span tok_span(const P *p, const Tok *t)
{
    Span sp = {0};

    sp.file_id = p->file_id;
    sp.line = t->line;
    sp.col = t->col;
    sp.len = t->len ? t->len : 1;
    return sp;
}

#define perr(p, t, ...)                                                        \
    do {                                                                       \
        if (!(p)->failed)                                                      \
            diag_emit((p)->dc, DIAG_ERROR, tok_span((p), (t)), __VA_ARGS__);   \
        (p)->failed = true;                                                    \
    } while (0)

/* --- lexer --------------------------------------------------------------- */

static bool is_ident_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_char(char c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9') || c == '.' ||
           c == '$' || c == '!';
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static u64 hex_val(char c)
{
    if (is_digit(c))
        return (u64)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (u64)(c - 'a' + 10);
    return (u64)(c - 'A' + 10);
}

/* Format 2 * value without requiring the mathematical result to fit in u64.
 * Initializer diagnostics name the expected hex-character count, which may be
 * as large as 2 * UINT64_MAX even though the parser must never compute that
 * product in the declared-size type. */
static void format_twice_u64(char out[21], u64 value)
{
    char rev[20];
    u32 n = 0;
    u32 carry = 0;
    u32 i;

    do {
        u32 doubled = (u32)(value % 10) * 2 + carry;

        rev[n++] = (char)('0' + doubled % 10);
        carry = doubled / 10;
        value /= 10;
    } while (value);
    if (carry)
        rev[n++] = (char)('0' + carry);
    for (i = 0; i < n; i++)
        out[i] = rev[n - i - 1];
    out[n] = '\0';
}

static bool lex_all(P *p, const char *src)
{
    const char *c = src;
    u32 line = 1;
    u32 col = 1;
    u32 cap = 256;

    p->toks = arena_alloc(p->arena, cap * sizeof(Tok), _Alignof(Tok));
    p->ntoks = 0;
    for (;;) {
        Tok t;

        /* skip whitespace and // comments */
        for (;;) {
            if (*c == '\n') {
                line++;
                col = 1;
                c++;
            } else if (*c == ' ' || *c == '\t' || *c == '\r') {
                col++;
                c++;
            } else if (c[0] == '/' && c[1] == '/') {
                while (*c && *c != '\n') {
                    c++;
                    col++;
                }
            } else {
                break;
            }
        }
        memset(&t, 0, sizeof(t));
        t.line = line;
        t.col = col;
        t.s = c;
        if (*c == '\0') {
            t.kind = T_EOF;
        } else if (is_ident_start(*c)) {
            const char *s = c;

            while (is_ident_char(*c)) {
                c++;
                col++;
            }
            t.kind = T_IDENT;
            t.len = (u32)(c - s);
        } else if (*c == '"') {
            /* Decoded into the arena as we go, so `s`/`len` name the VALUE
             * rather than the spelling; nothing downstream has to know the
             * bytes were ever quoted. */
            const char *open = c;
            char *dst;
            u32 n = 0;

            c++;
            col++;
            dst = arena_alloc(p->arena, strlen(c) + 1, 1);
            while (*c && *c != '"') {
                /* The four escapes print_quoted emits. `\n` and `\t` are
                 * DECODED rather than passed through because an asm
                 * template really contains those bytes -- passing the
                 * two-character spelling would hand the assembler a
                 * backslash-n and round-tripping would then differ from
                 * the original. */
                if (*c == '\\' && c[1] == 'n') {
                    dst[n++] = '\n';
                    c += 2;
                    col += 2;
                    continue;
                }
                if (*c == '\\' && c[1] == 't') {
                    dst[n++] = '\t';
                    c += 2;
                    col += 2;
                    continue;
                }
                if (*c == '\\' && (c[1] == '"' || c[1] == '\\')) {
                    c++;
                    col++;
                }
                if (*c == '\n') {
                    Tok bad = t;

                    bad.len = 1;
                    perr(p, &bad, "a newline inside a quoted string");
                    return false;
                }
                dst[n++] = *c++;
                col++;
            }
            if (*c != '"') {
                Tok bad = t;

                bad.len = (u32)(c - open);
                perr(p, &bad, "unterminated string");
                return false;
            }
            c++;
            col++;
            dst[n] = '\0';
            t.kind = T_STR;
            t.s = dst;
            t.len = n;
        } else if (*c == '%' || *c == '@') {
            const char *s = c;
            char intro = *c;
            bool exact_asm = false;

            c++;
            col++;
            if (intro == '@' && *c == '!') {
                exact_asm = true;
                c++;
                col++;
            }
            while (is_ident_char(*c) || is_digit(*c)) {
                c++;
                col++;
            }
            if (c - s == (exact_asm ? 2 : 1)) {
                Tok bad = t;

                bad.len = exact_asm ? 2 : 1;
                perr(p, &bad, "expected a name after '%c'", intro);
                return false;
            }
            t.kind = intro == '%' ? T_PIDENT : exact_asm ? T_XAIDENT : T_AIDENT;
            t.s = s + (exact_asm ? 2 : 1); /* name without the sigil */
            t.len = (u32)(c - t.s);
        } else if (c[0] == '0' && c[1] == 'x') {
            const char *s = c;
            u32 nd = 0;
            u64 v = 0;

            c += 2;
            col += 2;
            while (is_hex_digit(*c)) {
                if (nd == 16) {
                    Tok bad = t;

                    bad.len = (u32)(c - s);
                    perr(p, &bad, "hex constant wider than 64 bits");
                    return false;
                }
                v = v << 4 | hex_val(*c);
                nd++;
                c++;
                col++;
            }
            if (nd == 0) {
                Tok bad = t;

                bad.len = 2;
                perr(p, &bad, "expected hex digits after '0x'");
                return false;
            }
            t.kind = T_HEX;
            t.ival = v;
            t.len = (u32)(c - s);
        } else if (is_digit(*c) ||
                   ((*c == '-' || *c == '+') && is_digit(c[1]))) {
            const char *s = c;
            bool neg = *c == '-';
            u64 v = 0;

            if (*c == '-' || *c == '+') {
                c++;
                col++;
            }
            while (is_digit(*c)) {
                u64 d = (u64)(*c - '0');

                if (v > (0xFFFFFFFFFFFFFFFFull - d) / 10) {
                    Tok bad = t;

                    bad.len = (u32)(c - s + 1);
                    perr(p, &bad, "integer constant out of range");
                    return false;
                }
                v = v * 10 + d;
                c++;
                col++;
            }
            t.kind = T_INT;
            t.ival = neg ? (u64) - (i64)v : v;
            t.len = (u32)(c - s);
        } else if (c[0] == '.' && c[1] == '.' && c[2] == '.') {
            t.kind = T_ELLIPSIS;
            t.len = 3;
            c += 3;
            col += 3;
        } else {
            char ch = *c;

            switch (ch) {
            case '(':
                t.kind = T_LP;
                break;
            case ')':
                t.kind = T_RP;
                break;
            case '{':
                t.kind = T_LB;
                break;
            case '}':
                t.kind = T_RB;
                break;
            case ',':
                t.kind = T_COMMA;
                break;
            case ':':
                t.kind = T_COLON;
                break;
            case '=':
                t.kind = T_EQ;
                break;
            default:
                t.len = 1;
                perr(p, &t, "stray '%c' in IR", ch);
                return false;
            }
            t.len = 1;
            c++;
            col++;
        }
        if (p->ntoks == cap) {
            Tok *nt;

            cap *= 2;
            nt = arena_alloc(p->arena, cap * sizeof(Tok), _Alignof(Tok));
            memcpy(nt, p->toks, p->ntoks * sizeof(Tok));
            p->toks = nt;
        }
        p->toks[p->ntoks++] = t;
        if (t.kind == T_EOF)
            return true;
    }
}

/* --- token helpers ------------------------------------------------------- */

static Tok *peek(P *p)
{
    return &p->toks[p->pos];
}

static Tok *peek2(P *p)
{
    if (p->toks[p->pos].kind == T_EOF)
        return &p->toks[p->pos];
    return &p->toks[p->pos + 1];
}

static Tok *next(P *p)
{
    Tok *t = &p->toks[p->pos];

    if (t->kind != T_EOF)
        p->pos++;
    return t;
}

static bool tok_is(const Tok *t, const char *s)
{
    size_t n = strlen(s);

    return t->kind == T_IDENT && t->len == n && memcmp(t->s, s, n) == 0;
}

static Tok *expect(P *p, TokKind k, const char *what)
{
    Tok *t = next(p);

    if (t->kind != k) {
        perr(p, t, "expected %s", what);
        return NULL;
    }
    return t;
}

static bool tok_is_symbol(const Tok *t)
{
    return t->kind == T_AIDENT || t->kind == T_XAIDENT;
}

static Tok *expect_symbol(P *p, const char *what)
{
    Tok *t = next(p);

    if (!tok_is_symbol(t)) {
        perr(p, t, "expected %s", what);
        return NULL;
    }
    return t;
}

/* ` visibility(hidden)` on a global or a function. Absent means unspecified,
 * which is not the same as "default": an explicit default(1) survives the
 * round trip distinctly, because gcc lets a declaration say `default` to
 * override a -fvisibility= command-line setting. */
static u8 parse_visibility_suffix(P *p)
{
    Tok *v;

    if (!tok_is(peek(p), "visibility"))
        return GNU_VIS_UNSPEC;
    next(p);
    if (!expect(p, T_LP, "'(' after 'visibility'"))
        return GNU_VIS_UNSPEC;
    v = next(p);
    if (!v)
        return GNU_VIS_UNSPEC;
    (void)expect(p, T_RP, "')' after visibility");
    if (tok_is(v, "default"))
        return GNU_VIS_DEFAULT;
    if (tok_is(v, "hidden"))
        return GNU_VIS_HIDDEN;
    if (tok_is(v, "protected"))
        return GNU_VIS_PROTECTED;
    if (tok_is(v, "internal"))
        return GNU_VIS_INTERNAL;
    perr(p, v, "unknown visibility in IR text");
    return GNU_VIS_UNSPEC;
}

static const char *tok_name(P *p, const Tok *t)
{
    if (t->kind == T_XAIDENT) {
        char *name = arena_alloc(p->arena, t->len + 2, 1);

        name[0] = '!';
        memcpy(name + 1, t->s, t->len);
        name[t->len + 1] = '\0';
        return name;
    }
    return arena_strndup(p->arena, t->s, t->len);
}

/* ` section("name")`. Shared by the function and global headers so the two
 * cannot drift -- printing it in both and parsing it in neither is what made
 * -emit-ir ICE on every program using the attribute. */
static bool parse_section_marker(P *p, const char **out)
{
    Tok *nm;

    if (!tok_is(peek(p), "section"))
        return true;
    next(p);
    if (!expect(p, T_LP, "'(' after 'section'"))
        return false;
    nm = expect(p, T_STR, "a quoted section name");
    if (!nm)
        return false;
    *out = arena_strndup(p->arena, nm->s, nm->len);
    return expect(p, T_RP, "')' after the section name") != NULL;
}

/* ` constructor` / ` constructor(N)`, and the destructor spelling. The bare
 * form means CGF_INIT_PRIORITY_DEFAULT, which the printer omits for exactly
 * that reason -- it is the priority the attribute's own bare form carries. */
static bool parse_ctor_marker(P *p, const char *kw, bool *flag, u16 *prio)
{
    if (!tok_is(peek(p), kw))
        return true;
    next(p);
    *flag = true;
    *prio = (u16)CGF_INIT_PRIORITY_DEFAULT;
    if (peek(p)->kind != T_LP)
        return true;
    next(p);
    {
        Tok *pv = expect(p, T_INT, "a priority");

        if (!pv)
            return false;
        if (pv->ival > CGF_INIT_PRIORITY_DEFAULT) {
            perr(p, pv, "%s priority out of range", kw);
            return false;
        }
        *prio = (u16)pv->ival;
    }
    return expect(p, T_RP, "')' after the priority") != NULL;
}

static int lookup_type(const Tok *t)
{
    int i;

    for (i = 0; i <= IRT_VOID; i++)
        if (tok_is(t, ir_type_name((IrType)i)))
            return i;
    return -1;
}

static int lookup_op(const Tok *t)
{
    int i;

    for (i = 0; i <= IR_UNREACHABLE; i++)
        if (tok_is(t, ir_op_name((IrOp)i)))
            return i;
    return -1;
}

static bool parse_type(P *p, IrType *out, const char *what)
{
    Tok *t = next(p);
    int ty;

    if (t->kind != T_IDENT || (ty = lookup_type(t)) < 0) {
        perr(p, t, "expected %s", what);
        return false;
    }
    *out = (IrType)ty;
    return true;
}

/* --- value bookkeeping --------------------------------------------------- */

static void map_put_u32(P *p, Strmap *map, const char *key, size_t key_len,
                        u32 value)
{
    u32 *cell = arena_alloc(p->arena, sizeof(*cell), _Alignof(u32));

    *cell = value;
    strmap_put(map, key, key_len, cell);
}

static void def_value(P *p, const Tok *t, ValueId v)
{
    if (strmap_get(&p->vals, t->s, t->len)) {
        perr(p, t, "redefinition of value '%%%.*s'", (int)t->len, t->s);
        return;
    }
    map_put_u32(p, &p->vals, t->s, t->len, v.v);
}

/* Mirrors ir.c's new_value: parser-created values MUST appear in document
 * order, because the printer numbers them in document order and the ids
 * have to line up for parse(print(M)) == M. */
static ValueId parse_new_value(P *p, IrType t, IrValDef kind, u32 pos)
{
    IrFunc *f = p->f;
    ValueId v;

    if (f->nvals == f->cap_vals) {
        u32 nc = f->cap_vals ? f->cap_vals * 2 : 16;
        IrValInfo *nv =
            arena_alloc(p->arena, nc * sizeof(IrValInfo), _Alignof(IrValInfo));

        if (f->nvals)
            memcpy(nv, f->vals, f->nvals * sizeof(IrValInfo));
        f->vals = nv;
        f->cap_vals = nc;
    }
    f->vals[f->nvals].type = (u8)t;
    f->vals[f->nvals].def_kind = (u8)kind;
    f->vals[f->nvals].def_block = p->cur_block;
    f->vals[f->nvals].def_pos = pos;
    f->nvals++;
    v.v = f->nvals;
    return v;
}

static void add_fixup(P *p, IrOperand *slot, const Tok *t)
{
    if (p->nfixups == p->cap_fixups) {
        u32 nc = p->cap_fixups ? p->cap_fixups * 2 : 16;
        Fixup *nf = arena_alloc(p->arena, nc * sizeof(Fixup), _Alignof(Fixup));

        if (p->nfixups)
            memcpy(nf, p->fixups, p->nfixups * sizeof(Fixup));
        p->fixups = nf;
        p->cap_fixups = nc;
    }
    p->fixups[p->nfixups].slot = slot;
    p->fixups[p->nfixups].name = tok_name(p, t);
    p->fixups[p->nfixups].tok = *t;
    p->nfixups++;
}

/* --- operands ------------------------------------------------------------ */

static bool type_is_float(IrType t)
{
    return t >= IRT_F32 && t <= IRT_F128;
}

/* Parse an atom into *slot, which must be the FINAL arena location (fixups
 * point at it). `expected` types constants and undef; %values carry their
 * definition's type. */
static bool parse_atom(P *p, IrType expected, IrOperand *slot)
{
    Tok *t = next(p);

    memset(slot, 0, sizeof(*slot));
    switch (t->kind) {
    case T_PIDENT: {
        u32 *hit = strmap_get(&p->vals, t->s, t->len);

        if (hit) {
            ValueId v = {*hit};

            *slot = ir_op_value(p->f, v);
        } else {
            slot->kind = IROP_VALUE;
            slot->type = (u8)expected; /* fixed up when the def appears */
            add_fixup(p, slot, t);
        }
        return true;
    }
    case T_INT:
        if (ir_type_is_vector(expected)) {
            perr(p, t, "vector constants require 'vsplat' or 'load'");
            return false;
        }
        if (type_is_float(expected)) {
            perr(p, t,
                 "float constants are written as exact bits "
                 "(0x...); decimal would smuggle a host-float "
                 "conversion into the IR");
            return false;
        }
        *slot = ir_op_iconst(expected, (i64)t->ival);
        return true;
    case T_HEX:
        if (ir_type_is_vector(expected)) {
            perr(p, t, "vector constants require 'vsplat' or 'load'");
            return false;
        }
        if (type_is_float(expected)) {
            u64 lo = t->ival;
            u64 hi = 0;

            if (expected == IRT_F80 || expected == IRT_F128) {
                Tok *t2;

                hi = lo;
                if (!expect(p, T_COLON, "':' between fconst halves"))
                    return false;
                t2 = next(p);
                if (t2->kind != T_HEX) {
                    perr(p, t2,
                         "expected the low hex half of the "
                         "float constant");
                    return false;
                }
                lo = t2->ival;
            }
            /* Bits must FIT the format: an f32 with 33+ bits would be
             * silently masked by the printer and break the round-trip
             * fixpoint (found by ir_fuzz iteration 13208). */
            if ((expected == IRT_F32 && lo > 0xFFFFFFFFull) ||
                (expected == IRT_F80 && hi > 0xFFFFull)) {
                perr(p, t, "float constant bits exceed the %s format",
                     ir_type_name(expected));
                return false;
            }
            *slot = ir_op_fconst(expected, lo, hi);
        } else {
            *slot = ir_op_iconst(expected, (i64)t->ival);
        }
        return true;
    case T_AIDENT:
    case T_XAIDENT: {
        const char *nm = tok_name(p, t);
        i64 addend = 0;

        if (ir_type_is_vector(expected)) {
            perr(p, t, "vector operands must be SSA values or undef");
            return false;
        }
        if (peek(p)->kind == T_INT)
            addend = (i64)next(p)->ival;
        *slot = ir_op_symbol(expected, ir_sym(p->m, nm), addend);
        return true;
    }
    case T_IDENT:
        if (tok_is(t, "undef")) {
            *slot = ir_op_undef(expected);
            return true;
        }
        /* fall through to error */
        /* FALLTHROUGH */
    default:
        perr(p, t, "expected an operand");
        return false;
    }
}

static bool parse_typed(P *p, IrOperand *slot)
{
    IrType ty;

    if (!parse_type(p, &ty, "an operand type"))
        return false;
    return parse_atom(p, ty, slot);
}

/* A call argument: typed operand plus an optional ABI annotation
 * (`byval(N)`, `sret(N)`, `pair_xy(N)`), then an optional ` anon`, all
 * stored in operand.b. The printer emits them in that order. */
static bool parse_call_arg(P *p, IrOperand *slot)
{
    u32 kind = 0;

    if (!parse_typed(p, slot))
        return false;
    if (peek(p)->kind != T_IDENT)
        return true;
    if (tok_is(peek(p), "byval"))
        kind = IR_ARG_BYVAL;
    else if (tok_is(peek(p), "sret"))
        kind = IR_ARG_SRET;
    else if (tok_is(peek(p), "pair_ii"))
        kind = IR_ARG_PAIR_II;
    else if (tok_is(peek(p), "pair_is"))
        kind = IR_ARG_PAIR_IS;
    else if (tok_is(peek(p), "pair_si"))
        kind = IR_ARG_PAIR_SI;
    else if (tok_is(peek(p), "pair_ss"))
        kind = IR_ARG_PAIR_SS;
    else if (tok_is(peek(p), "hfa"))
        kind = IR_ARG_HFA;
    if (kind) {
        next(p);
        if (!expect(p, T_LP, "'(' after the argument annotation"))
            return false;
        {
            Tok *sz = expect(p, T_INT, "the annotated byte size");

            if (!sz)
                return false;
            if (kind == IR_ARG_HFA) {
                Tok *n;

                if (!expect(p, T_COMMA, "',' before the HFA leaf count"))
                    return false;
                n = expect(p, T_INT, "the HFA leaf count");
                if (!n)
                    return false;
                slot->b = ir_arg_annot_hfa((u32)sz->ival, (u32)n->ival);
            } else {
                slot->b = ir_arg_annot(kind, (u32)sz->ival);
            }
        }
        if (!expect(p, T_RP, "')'"))
            return false;
        if (peek(p)->kind != T_IDENT)
            return true;
    }
    /* The argument FLAGS are not kinds: they compose with each other and
     * with the annotation, and ride their own byte. The printer emits them
     * in this order. */
    if (tok_is(peek(p), "anon")) {
        next(p);
        slot->argflags |= IROPF_ANON;
        if (peek(p)->kind != T_IDENT)
            return true;
    }
    if (tok_is(peek(p), "sext")) {
        next(p);
        slot->argflags |= IROPF_SEXT;
    } else if (tok_is(peek(p), "zext")) {
        next(p);
        slot->argflags |= IROPF_ZEXT;
    }
    if (peek(p)->kind == T_IDENT && tok_is(peek(p), "onstack")) {
        next(p);
        slot->argflags |= IROPF_ONSTACK;
    }
    if (peek(p)->kind == T_IDENT && tok_is(peek(p), "stackalign16")) {
        if (slot->kind != IROP_VALUE && slot->kind != IROP_SYMBOL) {
            perr(p, peek(p), "'stackalign16' requires an SSA value or symbol");
            return false;
        }
        next(p);
        slot->b |= IR_ABI_STACK_ALIGN16;
    }
    if (peek(p)->kind == T_IDENT && tok_is(peek(p), "even")) {
        if (slot->kind != IROP_VALUE && slot->kind != IROP_SYMBOL) {
            perr(p, peek(p), "'even' requires an SSA value or symbol");
            return false;
        }
        next(p);
        slot->b |= IR_ABI_EVEN_GPR;
    }
    return true;
}

/* Count the comma-separated items between the current '(' and its
 * MATCHING ')'. Depth-aware since Sprint 19: call-arg annotations like
 * `byval(24)` nest one paren level. The count still lets operand arrays
 * be arena-allocated at FINAL size before parsing, which is what makes
 * fixup pointers stable. */
static bool count_args(P *p, u32 *out)
{
    u32 i = p->pos;
    u32 n = 0;
    u32 depth = 0;

    if (p->toks[i].kind == T_RP) {
        *out = 0;
        return true;
    }
    n = 1;
    for (;; i++) {
        if (p->toks[i].kind == T_EOF || p->toks[i].kind == T_RB) {
            perr(p, &p->toks[i], "unclosed '(' in argument list");
            return false;
        }
        if (p->toks[i].kind == T_LP)
            depth++;
        else if (p->toks[i].kind == T_RP) {
            if (depth == 0)
                break;
            depth--;
        } else if (p->toks[i].kind == T_COMMA && depth == 0)
            n++;
    }
    *out = n;
    return true;
}

/* --- instructions -------------------------------------------------------- */

/* The parser's own append: same list surgery as the builder, but NO
 * append-after-terminator ICE — malformed text must reach the verifier
 * (which rejects it with a real diagnostic), not kill the compiler. */
static IrInst *inst_append(P *p, IrOp op, IrType ty, const Tok *res)
{
    IrBlock *blk = ir_block(p->f, p->cur_block);
    IrInst *in = arena_alloc(p->arena, sizeof(IrInst), _Alignof(IrInst));

    memset(in, 0, sizeof(*in));
    in->op = (u8)op;
    in->type = (u8)ty;
    if (res) {
        ValueId v = parse_new_value(p, ty, VDEF_INST, blk->ninsts);

        def_value(p, res, v);
        in->result = v;
    }
    if (blk->last)
        blk->last->next = in;
    else
        blk->first = in;
    blk->last = in;
    blk->ninsts++;
    return in;
}

static IrOperand *ops_alloc(P *p, u32 n)
{
    IrOperand *o;

    if (n == 0)
        return NULL;
    o = arena_alloc(p->arena, n * sizeof(IrOperand), _Alignof(IrOperand));
    memset(o, 0, n * sizeof(IrOperand));
    return o;
}

static bool parse_edge(P *p, IrEdge *e)
{
    Tok *lbl = expect(p, T_IDENT, "a branch target label");
    u32 *hit;
    u32 i;

    if (!lbl)
        return false;
    hit = strmap_get(&p->blocks, lbl->s, lbl->len);
    if (!hit) {
        perr(p, lbl, "branch to unknown block '%.*s'", (int)lbl->len, lbl->s);
        return false;
    }
    e->target.v = *hit;
    /* case_val is the CALLER's field (already set for switch cases,
     * memset-zero otherwise) — do not touch it here. */
    if (!expect(p, T_LP, "'(' after the branch target"))
        return false;
    if (!count_args(p, &e->nargs))
        return false;
    e->args = ops_alloc(p, e->nargs);
    for (i = 0; i < e->nargs; i++) {
        if (i && !expect(p, T_COMMA, "','"))
            return false;
        if (!parse_typed(p, &e->args[i]))
            return false;
    }
    return expect(p, T_RP, "')'") != NULL;
}

static IrEdge *edges_alloc(P *p, u32 n)
{
    IrEdge *e = arena_alloc(p->arena, n * sizeof(IrEdge), _Alignof(IrEdge));

    memset(e, 0, n * sizeof(IrEdge));
    return e;
}

/* `align N` clause; alignment fits u32 or it's an error. */
static bool parse_align(P *p, u32 *out)
{
    Tok *t;

    if (!expect(p, T_COMMA, "', align N'"))
        return false;
    t = next(p);
    if (!tok_is(t, "align")) {
        perr(p, t, "expected 'align'");
        return false;
    }
    t = expect(p, T_INT, "an alignment value");
    if (!t)
        return false;
    if (t->ival > 0xFFFFFFFFull) {
        perr(p, t, "alignment out of range");
        return false;
    }
    *out = (u32)t->ival;
    return true;
}

static u8 parse_memflags(P *p)
{
    u8 flags = 0;

    while (peek(p)->kind == T_COMMA) {
        Tok *t = peek2(p);

        if (tok_is(t, "volatile"))
            flags |= IRF_VOLATILE;
        else if (tok_is(t, "seq_cst"))
            flags |= IRF_SEQ_CST;
        else if (tok_is(t, "self_init"))
            flags |= IRF_SELF_INIT;
        else
            break;
        next(p);
        next(p);
    }
    return flags;
}

static bool parse_flow_provenance(P *p, IrInst *in, const char *name)
{
    if (peek(p)->kind != T_COMMA || !tok_is(peek2(p), name))
        return true;
    next(p);
    next(p);
    in->flags |= IRF_FLOW_PROVENANCE;
    return true;
}

static bool parse_etype(P *p, u8 *out)
{
    Tok *name;
    int i;

    if (peek(p)->kind != T_COMMA || !tok_is(peek2(p), "etype"))
        return true;
    next(p);
    next(p);
    name = expect(p, T_IDENT, "an effective-type name");
    if (!name)
        return false;
    for (i = 0; i < ETYPE_COUNT; i++) {
        if (tok_is(name, ir_etype_name((EffTypeId)i))) {
            *out = (u8)i;
            return true;
        }
    }
    perr(p, name, "unknown effective type '%.*s'", (int)name->len, name->s);
    return false;
}

/* An IR_ASM instruction, matching print.c's rendering exactly:
 *
 *   asm [volatile] [basic] "template"
 *       [, (out|in)[&][=N] "constraint" <atom>]...
 *       [, clobbers [memory] [cc] [rK]...]
 *
 * The whole record is inline rather than a reference into a module table, so
 * one line of IR text is one asm and the round trip needs no side channel.
 * Registers print as rK because the NUMBER is what the backend consumes; the
 * letter that produced it was target vocabulary and is already decoded. */
static bool parse_asm_inst(P *p)
{
    IrAsm a;
    IrAsmOp ops[64];
    IrOperand vals[64];
    u8 clob[64];
    u32 n = 0;
    u32 nclob = 0;
    IrInst *in;
    Tok *t;

    memset(&a, 0, sizeof(a));
    if (tok_is(peek(p), "volatile")) {
        next(p);
        a.is_volatile = true;
    }
    if (tok_is(peek(p), "basic")) {
        next(p);
        a.is_basic = true;
    }
    t = expect(p, T_STR, "an asm template string");
    if (!t)
        return false;
    a.tmpl = t->s;

    while (peek(p)->kind == T_COMMA) {
        next(p);
        if (tok_is(peek(p), "clobbers")) {
            next(p);
            for (;;) {
                Tok *c = peek(p);

                if (tok_is(c, "memory")) {
                    next(p);
                    a.clobbers_memory = true;
                } else if (tok_is(c, "cc")) {
                    next(p);
                    a.clobbers_cc = true;
                } else if (c->kind == T_IDENT && c->len > 1 && c->s[0] == 'r' &&
                           c->s[1] >= '0' && c->s[1] <= '9') {
                    {
                        u32 v = 0;
                        u32 k;

                        for (k = 1; k < c->len; k++)
                            v = v * 10 + (u32)(c->s[k] - '0');
                        if (nclob < 64)
                            clob[nclob++] = (u8)v;
                    }
                    next(p);
                } else {
                    break;
                }
            }
            continue;
        }
        if (n >= 64) {
            perr(p, peek(p), "too many asm operands");
            return false;
        }
        memset(&ops[n], 0, sizeof(ops[n]));
        ops[n].tied_to = -1;
        if (tok_is(peek(p), "out")) {
            ops[n].is_output = true;
        } else if (!tok_is(peek(p), "in")) {
            perr(p, peek(p), "expected 'out' or 'in' for an asm operand");
            return false;
        }
        next(p);
        if (peek(p)->kind == T_IDENT && peek(p)->len == 1 &&
            peek(p)->s[0] == '&') {
            next(p);
            ops[n].early_clobber = true;
        }
        if (peek(p)->kind == T_EQ) {
            Tok *iv;

            next(p);
            iv = expect(p, T_INT, "a tied operand index");
            if (!iv)
                return false;
            ops[n].tied_to = (i32)iv->ival;
        }
        t = expect(p, T_STR, "an asm operand constraint");
        if (!t)
            return false;
        ops[n].constraint = t->s;
        if (!parse_typed(p, &vals[n]))
            return false;
        n++;
    }
    if (n) {
        a.ops = arena_alloc(p->arena, n * sizeof(IrAsmOp), _Alignof(IrAsmOp));
        memcpy(a.ops, ops, n * sizeof(IrAsmOp));
    }
    a.nops = n;
    for (a.noutputs = 0; a.noutputs < n && ops[a.noutputs].is_output;)
        a.noutputs++;
    if (nclob) {
        a.clobber_regs = arena_alloc(p->arena, nclob, 1);
        memcpy(a.clobber_regs, clob, nclob);
        a.nclobber_regs = nclob;
    }
    in = inst_append(p, IR_ASM, IRT_VOID, NULL);
    in->callee = ir_asm_new(p->m, &a);
    if (n) {
        in->ops = ops_alloc(p, n);
        in->nops = n;
        memcpy(in->ops, vals, n * sizeof(IrOperand));
    }
    return true;
}

static bool parse_inst(P *p)
{
    Tok *res = NULL;
    Tok *opt;
    int op;
    IrInst *in;
    IrType ty;
    bool has_nsw = false;

    if (peek(p)->kind == T_PIDENT) {
        res = next(p);
        if (!expect(p, T_EQ, "'=' after the result name"))
            return false;
    }
    opt = next(p);
    op = opt->kind == T_IDENT ? lookup_op(opt) : -1;
    if (op < 0) {
        perr(p, opt, "expected an instruction");
        return false;
    }
    switch ((IrOp)op) {
    case IR_STORE:
    case IR_MEMCPY:
    case IR_MEMSET:
    case IR_VA_START:
    case IR_STACKRESTORE:
    case IR_ASM:
    case IR_RET:
    case IR_BR:
    case IR_CONDBR:
    case IR_SWITCH:
    case IR_UNREACHABLE:
        if (res) {
            perr(p, res, "'%s' does not produce a value", ir_op_name((IrOp)op));
            return false;
        }
        break;
    default:
        if (!res && (IrOp)op != IR_CALL) {
            perr(p, opt, "'%s' produces a value; write '%%name = ...'",
                 ir_op_name((IrOp)op));
            return false;
        }
        break;
    }
    switch ((IrOp)op) {
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
        if (((IrOp)op == IR_IADD || (IrOp)op == IR_ISUB ||
             (IrOp)op == IR_IMUL) &&
            tok_is(peek(p), "nsw")) {
            next(p);
            has_nsw = true;
        }
        if (!parse_type(p, &ty, "the operand type"))
            return false;
        in = inst_append(p, (IrOp)op, ty, res);
        if (has_nsw)
            in->flags |= IRF_NSW;
        in->ops = ops_alloc(p, 2);
        in->nops = 2;
        if (!parse_atom(p, ty, &in->ops[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        if (!parse_atom(p, ty, &in->ops[1]))
            return false;
        if (((IrOp)op == IR_IADD || (IrOp)op == IR_ISUB ||
             (IrOp)op == IR_IMUL) &&
            peek(p)->kind == T_COMMA && tok_is(peek2(p), "nsw")) {
            next(p);
            next(p);
            in->flags |= IRF_NSW;
        }
        return true;
    case IR_ICMP:
    case IR_FCMP: {
        Tok *pt = expect(p, T_IDENT, "a comparison predicate");
        int pred = -1;
        int i;

        if (!pt)
            return false;
        if ((IrOp)op == IR_ICMP) {
            for (i = 0; i <= ICMP_UGE; i++)
                if (tok_is(pt, ir_icmp_name((IrIcmp)i)))
                    pred = i;
        } else {
            for (i = 0; i <= FCMP_UNO; i++)
                if (tok_is(pt, ir_fcmp_name((IrFcmp)i)))
                    pred = i;
        }
        if (pred < 0) {
            perr(p, pt, "unknown %s predicate '%.*s'",
                 (IrOp)op == IR_ICMP ? "icmp" : "fcmp", (int)pt->len, pt->s);
            return false;
        }
        if (!parse_type(p, &ty, "the operand type"))
            return false;
        in = inst_append(p, (IrOp)op, IRT_I32, res);
        in->subop = (u8)pred;
        in->ops = ops_alloc(p, 2);
        in->nops = 2;
        if (!parse_atom(p, ty, &in->ops[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        if (!parse_atom(p, ty, &in->ops[1]))
            return false;
        if ((IrOp)op == IR_ICMP && peek(p)->kind == T_COMMA &&
            tok_is(peek2(p), "bounds")) {
            next(p);
            next(p);
            in->flags |= IRF_BOUNDS_CHECK;
        }
        return true;
    }
    case IR_FNEG:
        if (!parse_type(p, &ty, "the operand type"))
            return false;
        in = inst_append(p, IR_FNEG, ty, res);
        in->ops = ops_alloc(p, 1);
        in->nops = 1;
        return parse_atom(p, ty, &in->ops[0]);
    case IR_VSPLAT: {
        IrType et;

        if (!parse_type(p, &ty, "the vector result type"))
            return false;
        et = ir_vector_elem_type(ty);
        in = inst_append(p, IR_VSPLAT, ty, res);
        in->ops = ops_alloc(p, 1);
        in->nops = 1;
        if (!parse_type(p, &ty, "the scalar element type"))
            return false;
        if (ty != et) {
            perr(p, peek(p), "vsplat scalar type does not match vector lanes");
            return false;
        }
        return parse_atom(p, et, &in->ops[0]);
    }
    case IR_VEXTRACT: {
        Tok *lane;

        if (!parse_type(p, &ty, "the vector operand type"))
            return false;
        in = inst_append(p, IR_VEXTRACT, ir_vector_elem_type(ty), res);
        in->ops = ops_alloc(p, 1);
        in->nops = 1;
        if (!parse_atom(p, ty, &in->ops[0]) ||
            !expect(p, T_COMMA, "',' before the lane index"))
            return false;
        lane = next(p);
        if (lane->kind != T_INT || lane->ival > 255) {
            perr(p, lane, "expected a compile-time vector lane index");
            return false;
        }
        in->subop = (u8)lane->ival;
        return true;
    }
    case IR_VREDUCE_ADD:
    case IR_VREDUCE_MUL:
    case IR_VREDUCE_AND:
    case IR_VREDUCE_OR:
    case IR_VREDUCE_XOR:
        if (!parse_type(p, &ty, "the vector operand type"))
            return false;
        in = inst_append(p, (IrOp)op, ir_vector_elem_type(ty), res);
        in->ops = ops_alloc(p, 1);
        in->nops = 1;
        return parse_atom(p, ty, &in->ops[0]);
    case IR_SEXT:
    case IR_ZEXT:
    case IR_TRUNC:
    case IR_FPEXT:
    case IR_FPTRUNC:
    case IR_FPTOSI:
    case IR_FPTOUI:
    case IR_SITOFP:
    case IR_UITOFP:
    case IR_BITCAST: {
        IrType from, to;
        Tok *kw;
        /* The operand parses BEFORE 'to <ty>' is known, so its final
         * arena slot is allocated up front (a fixup may point at it) and
         * the instruction adopts the array afterwards. */
        IrOperand *slot = ops_alloc(p, 1);

        if (!parse_type(p, &from, "the source type"))
            return false;
        if (!parse_atom(p, from, &slot[0]))
            return false;
        kw = next(p);
        if (!tok_is(kw, "to")) {
            perr(p, kw, "expected 'to' after the conversion operand");
            return false;
        }
        if (!parse_type(p, &to, "the result type"))
            return false;
        in = inst_append(p, (IrOp)op, to, res);
        in->ops = slot;
        in->nops = 1;
        return true;
    }
    case IR_ALLOCA:
        in = inst_append(p, IR_ALLOCA, IRT_PTR, res);
        in->ops = ops_alloc(p, 1);
        in->nops = 1;
        if (!parse_atom(p, IRT_I64, &in->ops[0]))
            return false;
        if (!parse_align(p, &in->align))
            return false;
        return parse_etype(p, &in->subop);
    case IR_LOAD:
        if (!parse_type(p, &ty, "the loaded type"))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        in = inst_append(p, IR_LOAD, ty, res);
        in->ops = ops_alloc(p, 1);
        in->nops = 1;
        if (!parse_atom(p, IRT_PTR, &in->ops[0]))
            return false;
        if (!parse_align(p, &in->align))
            return false;
        in->flags = parse_memflags(p);
        return parse_etype(p, &in->subop);
    case IR_STORE:
        in = inst_append(p, IR_STORE, IRT_VOID, NULL);
        in->ops = ops_alloc(p, 2);
        in->nops = 2;
        if (!parse_typed(p, &in->ops[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        if (!parse_atom(p, IRT_PTR, &in->ops[1]))
            return false;
        if (!parse_align(p, &in->align))
            return false;
        in->flags = parse_memflags(p);
        return parse_etype(p, &in->subop);
    case IR_PTRADD:
        in = inst_append(p, IR_PTRADD, IRT_PTR, res);
        in->ops = ops_alloc(p, 2);
        in->nops = 2;
        if (!parse_atom(p, IRT_PTR, &in->ops[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        return parse_atom(p, IRT_I64, &in->ops[1]);
    case IR_MEMCPY:
    case IR_MEMSET:
        in = inst_append(p, (IrOp)op, IRT_VOID, NULL);
        in->subop = ETYPE_CHAR;
        in->ops = ops_alloc(p, 3);
        in->nops = 3;
        if (!parse_atom(p, IRT_PTR, &in->ops[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        if (!parse_atom(p, (IrOp)op == IR_MEMCPY ? IRT_PTR : IRT_I32,
                        &in->ops[1]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        if (!parse_atom(p, IRT_I64, &in->ops[2]))
            return false;
        if (!parse_align(p, &in->align))
            return false;
        if (!parse_etype(p, &in->subop))
            return false;
        in->flags = parse_memflags(p);
        return true;
    case IR_SELECT: {
        IrOperand *slots = ops_alloc(p, 3); /* final home before parsing:
                                               fixups need stable slots */

        if (!parse_atom(p, IRT_I32, &slots[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        if (!parse_type(p, &ty, "the arm type"))
            return false;
        in = inst_append(p, IR_SELECT, ty, res);
        in->ops = slots;
        in->nops = 3;
        if (!parse_atom(p, ty, &slots[1]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        return parse_atom(p, ty, &slots[2]);
    }
    case IR_CALL: {
        Tok *ct;
        u32 nargs;
        u32 i;
        bool indirect;
        IrOperand *fp = NULL;
        u32 fp_fixup = 0;

        if (!parse_type(p, &ty, "the return type"))
            return false;
        if (res && ty == IRT_VOID) {
            perr(p, res, "a void call cannot name a result");
            return false;
        }
        if (!res && ty != IRT_VOID) {
            perr(p, opt,
                 "a non-void call names its result: "
                 "'%%name = call ...'");
            return false;
        }
        ct = peek(p);
        indirect = !tok_is_symbol(ct);
        if (indirect) {
            /* The pointer parses before the arg count is known, so it
             * gets a staging arena slot; if a fixup landed on it, the
             * fixup is re-pointed after the copy into the final array. */
            fp = ops_alloc(p, 1);
            fp_fixup = p->nfixups;
            if (!parse_atom(p, IRT_PTR, &fp[0]))
                return false;
        } else {
            next(p);
        }
        if (!expect(p, T_LP, "'(' before the argument list"))
            return false;
        if (!count_args(p, &nargs))
            return false;
        in = inst_append(p, IR_CALL, ty, res);
        in->ops = ops_alloc(p, nargs + (indirect ? 1u : 0u));
        in->nops = nargs + (indirect ? 1u : 0u);
        if (indirect) {
            in->subop = FUNCREF_INDIRECT;
            in->ops[0] = fp[0];
            if (p->nfixups > fp_fixup &&
                p->fixups[p->nfixups - 1].slot == &fp[0])
                p->fixups[p->nfixups - 1].slot = &in->ops[0];
        } else {
            const char *callee_name = tok_name(p, ct);
            u32 *hit =
                strmap_get(&p->func_ids, callee_name, strlen(callee_name));

            if (hit) {
                in->subop = FUNCREF_INTERNAL;
                in->callee = *hit - 1;
            } else {
                in->subop = FUNCREF_EXTERNAL;
                in->callee = ir_sym(p->m, callee_name);
            }
        }
        for (i = 0; i < nargs; i++) {
            if (i && !expect(p, T_COMMA, "','"))
                return false;
            if (!parse_call_arg(p, &in->ops[i + (indirect ? 1u : 0u)]))
                return false;
        }
        if (!expect(p, T_RP, "')'"))
            return false;
        if (tok_is(peek(p), "va")) {
            next(p);
            in->flags |= IRF_CALL_VARIADIC;
        }
        if (tok_is(peek(p), "noreturn")) {
            next(p);
            in->flags |= IRF_NORETURN;
        }
        return true;
    }
    case IR_VA_START:
        in = inst_append(p, IR_VA_START, IRT_VOID, NULL);
        in->ops = ops_alloc(p, 1);
        in->nops = 1;
        return parse_atom(p, IRT_PTR, &in->ops[0]);
    case IR_STACKSAVE:
        (void)inst_append(p, IR_STACKSAVE, IRT_PTR, res);
        return true;
    case IR_STACKRESTORE:
        in = inst_append(p, IR_STACKRESTORE, IRT_VOID, NULL);
        in->ops = ops_alloc(p, 1);
        in->nops = 1;
        return parse_atom(p, IRT_PTR, &in->ops[0]);
    case IR_ASM:
        return parse_asm_inst(p);
    case IR_ATOMICRMW: {
        Tok *kt = expect(p, T_IDENT, "an atomicrmw operation");
        int rk = -1;
        int i2;

        if (!kt)
            return false;
        for (i2 = 0; i2 <= RMW_XCHG; i2++)
            if (tok_is(kt, ir_rmw_name((u8)i2)))
                rk = i2;
        if (rk < 0) {
            perr(p, kt, "unknown atomicrmw operation '%.*s'", (int)kt->len,
                 kt->s);
            return false;
        }
        if (!parse_type(p, &ty, "the value type"))
            return false;
        in = inst_append(p, IR_ATOMICRMW, ty, res);
        in->subop = (u8)rk;
        in->ops = ops_alloc(p, 2);
        in->nops = 2;
        if (!parse_atom(p, IRT_PTR, &in->ops[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        if (!parse_atom(p, ty, &in->ops[1]))
            return false;
        in->flags = parse_memflags(p);
        return true;
    }
    case IR_CMPXCHG:
        if (!parse_type(p, &ty, "the value type"))
            return false;
        in = inst_append(p, IR_CMPXCHG, ty, res);
        in->ops = ops_alloc(p, 3);
        in->nops = 3;
        if (!parse_atom(p, IRT_PTR, &in->ops[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        if (!parse_atom(p, ty, &in->ops[1]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        if (!parse_atom(p, ty, &in->ops[2]))
            return false;
        in->flags = parse_memflags(p);
        return true;
    case IR_RET:
        in = inst_append(p, IR_RET, IRT_VOID, NULL);
        if (peek(p)->kind == T_IDENT && lookup_type(peek(p)) >= 0) {
            in->ops = ops_alloc(p, 1);
            in->nops = 1;
            if (!parse_typed(p, &in->ops[0]))
                return false;
        }
        return parse_flow_provenance(p, in, "implicit");
    case IR_BR:
        in = inst_append(p, IR_BR, IRT_VOID, NULL);
        in->edges = edges_alloc(p, 1);
        in->nedges = 1;
        if (!parse_edge(p, &in->edges[0]))
            return false;
        return parse_flow_provenance(p, in, "defensive");
    case IR_CONDBR:
        in = inst_append(p, IR_CONDBR, IRT_VOID, NULL);
        in->ops = ops_alloc(p, 1);
        in->nops = 1;
        if (!parse_atom(p, IRT_I32, &in->ops[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        in->edges = edges_alloc(p, 2);
        in->nedges = 2;
        if (!parse_edge(p, &in->edges[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        if (!parse_edge(p, &in->edges[1]))
            return false;
        return parse_flow_provenance(p, in, "config");
    case IR_SWITCH: {
        u32 cap = 4;
        IrEdge *edges;

        if (!parse_type(p, &ty, "the switch operand type"))
            return false;
        in = inst_append(p, IR_SWITCH, IRT_VOID, NULL);
        in->ops = ops_alloc(p, 1);
        in->nops = 1;
        if (!parse_atom(p, ty, &in->ops[0]))
            return false;
        if (!expect(p, T_COMMA, "','"))
            return false;
        edges = edges_alloc(p, cap);
        in->edges = edges;
        in->nedges = 1;
        if (!parse_edge(p, &edges[0]))
            return false;
        /* `, INT: edge` repeats. Edge arg lists live in their own arena
         * arrays, so growing the edge ARRAY does not move fixup targets
         * (fixups only ever point into args arrays). */
        while (peek(p)->kind == T_COMMA && peek2(p)->kind == T_INT) {
            Tok *cv;

            next(p);
            cv = next(p);
            if (!expect(p, T_COLON, "':' after the case value"))
                return false;
            if (in->nedges == cap) {
                IrEdge *ne;

                cap *= 2;
                ne = edges_alloc(p, cap);
                memcpy(ne, edges, in->nedges * sizeof(IrEdge));
                edges = ne;
                in->edges = ne;
            }
            edges[in->nedges].case_val = (i64)cv->ival;
            if (!parse_edge(p, &edges[in->nedges]))
                return false;
            in->nedges++;
        }
        return parse_flow_provenance(p, in, "config");
    }
    case IR_UNREACHABLE:
        inst_append(p, IR_UNREACHABLE, IRT_VOID, NULL);
        return true;
    default:
        perr(p, opt, "unhandled opcode in parser");
        return false;
    }
}

/* --- blocks and functions ------------------------------------------------ */

/* Label pre-scan: a bare IDENT + balanced (...) followed by ':' is a block
 * label; nothing else in the grammar matches that shape (edge targets are
 * followed by ',' / ')' / an instruction, never ':'). Finds every block
 * before any branch parses, so forward branch targets just work. */
static bool prescan_blocks(P *p)
{
    u32 i = p->pos;

    while (p->toks[i].kind != T_RB) {
        if (p->toks[i].kind == T_EOF) {
            perr(p, &p->toks[i], "missing '}' at end of function");
            return false;
        }
        if (p->toks[i].kind == T_IDENT && p->toks[i + 1].kind == T_LP) {
            u32 j = i + 2;

            while (p->toks[j].kind != T_RP && p->toks[j].kind != T_EOF)
                j++;
            if (p->toks[j].kind == T_RP && p->toks[j + 1].kind == T_COLON) {
                Tok *lbl = &p->toks[i];
                BlockId b;

                if (strmap_get(&p->blocks, lbl->s, lbl->len)) {
                    perr(p, lbl, "duplicate block label '%.*s'", (int)lbl->len,
                         lbl->s);
                    return false;
                }
                if (lookup_op(lbl) >= 0 || lookup_type(lbl) >= 0 ||
                    tok_is(lbl, "undef")) {
                    perr(p, lbl,
                         "'%.*s' is reserved and cannot label "
                         "a block",
                         (int)lbl->len, lbl->s);
                    return false;
                }
                b = ir_block_new(p->m, p->f, tok_name(p, lbl));
                map_put_u32(p, &p->blocks, lbl->s, lbl->len, b.v);
                i = j + 2;
                continue;
            }
        }
        i++;
    }
    return true;
}

static bool parse_block_header(P *p)
{
    Tok *lbl = expect(p, T_IDENT, "a block label");
    u32 *hit;

    if (!lbl)
        return false;
    hit = strmap_get(&p->blocks, lbl->s, lbl->len);
    if (!hit) {
        perr(p, lbl, "expected a block label");
        return false;
    }
    p->cur_block.v = *hit;
    if (!expect(p, T_LP, "'(' after the block label"))
        return false;
    if (peek(p)->kind != T_RP) {
        for (;;) {
            IrType ty;
            Tok *pn;
            ValueId v;

            if (!parse_type(p, &ty, "a block parameter type"))
                return false;
            pn = expect(p, T_PIDENT, "a block parameter name");
            if (!pn)
                return false;
            v = ir_block_param(p->m, p->f, p->cur_block, ty);
            def_value(p, pn, v);
            if (p->failed)
                return false;
            if (peek(p)->kind != T_COMMA)
                break;
            next(p);
        }
    }
    if (!expect(p, T_RP, "')'"))
        return false;
    return expect(p, T_COLON, "':' after the block header") != NULL;
}

static bool parse_func(P *p)
{
    IrType ret;
    Tok *nm;
    IrType ptypes[64];
    Tok *pnames[64];
    u64 pannots[64];
    bool any_annot = false;
    u32 nparams = 0;
    bool variadic = false;
    bool unprototyped = false;
    bool fn_weak = false;
    u8 fn_visibility = GNU_VIS_UNSPEC;
    u32 fn_align = 0;
    bool fn_used = false;
    const char *fn_section = NULL;
    bool fn_ctor = false;
    bool fn_dtor = false;
    u16 fn_ctor_prio = (u16)CGF_INIT_PRIORITY_DEFAULT;
    u16 fn_dtor_prio = (u16)CGF_INIT_PRIORITY_DEFAULT;
    bool internal_marker = false;
    bool setjmp_marker = false;
    bool contract_marker = false;
    u8 abi_ret = IR_ABIRET_NONE;
    u8 abi_ret_n = 0;
    IrFunc *f;
    u32 i;

    if (!parse_type(p, &ret, "the return type"))
        return false;
    nm = expect_symbol(p, "the function name");
    if (!nm)
        return false;
    if (!expect(p, T_LP, "'('"))
        return false;
    if (peek(p)->kind == T_ELLIPSIS) {
        next(p);
        variadic = true;
    } else if (peek(p)->kind != T_RP) {
        for (;;) {
            IrType ty;
            Tok *pn;

            if (nparams == 64) {
                perr(p, peek(p), "more than 64 parameters");
                return false;
            }
            if (!parse_type(p, &ty, "a parameter type"))
                return false;
            pannots[nparams] = 0;
            if (tok_is(peek(p), "byval")) {
                Tok *sz;

                next(p);
                if (!expect(p, T_LP, "'(' after 'byval'"))
                    return false;
                sz = expect(p, T_INT, "the byval size");
                if (!sz)
                    return false;
                if (!expect(p, T_RP, "')'"))
                    return false;
                pannots[nparams] = ir_arg_annot(IR_ARG_BYVAL, (u32)sz->ival);
                any_annot = true;
            }
            if (tok_is(peek(p), "onstack")) {
                next(p);
                pannots[nparams] |= IR_PARAM_ONSTACK;
                any_annot = true;
            }
            if (tok_is(peek(p), "stackalign16")) {
                next(p);
                pannots[nparams] |= IR_ABI_STACK_ALIGN16;
                any_annot = true;
            }
            if (tok_is(peek(p), "even")) {
                next(p);
                pannots[nparams] |= IR_ABI_EVEN_GPR;
                any_annot = true;
            }
            if (tok_is(peek(p), "restrict")) {
                next(p);
                pannots[nparams] |= IR_PARAM_RESTRICT;
                any_annot = true;
            }
            pn = expect(p, T_PIDENT, "a parameter name");
            if (!pn)
                return false;
            ptypes[nparams] = ty;
            pnames[nparams] = pn;
            nparams++;
            if (peek(p)->kind != T_COMMA)
                break;
            next(p);
            if (peek(p)->kind == T_ELLIPSIS) {
                next(p);
                variadic = true;
                break;
            }
        }
    }
    if (!expect(p, T_RP, "')'"))
        return false;
    if (tok_is(peek(p), "unproto")) {
        next(p);
        unprototyped = true;
    }
    if (tok_is(peek(p), "internal")) {
        next(p);
        internal_marker = true;
    }
    if (tok_is(peek(p), "weak")) {
        next(p);
        fn_weak = true;
    }
    fn_visibility = parse_visibility_suffix(p);
    if (tok_is(peek(p), "used")) {
        next(p);
        fn_used = true;
    }
    if (tok_is(peek(p), "align")) {
        Tok *av;

        next(p);
        if (!expect(p, T_LP, "'(' after 'align'"))
            return false;
        av = expect(p, T_INT, "a function alignment");
        if (!av)
            return false;
        fn_align = (u32)av->ival;
        if (!expect(p, T_RP, "')' after the function alignment"))
            return false;
    }
    if (!parse_section_marker(p, &fn_section))
        return false;
    if (!parse_ctor_marker(p, "constructor", &fn_ctor, &fn_ctor_prio))
        return false;
    if (!parse_ctor_marker(p, "destructor", &fn_dtor, &fn_dtor_prio))
        return false;
    if (tok_is(peek(p), "abi")) {
        Tok *an;

        next(p);
        if (!expect(p, T_LP, "'(' after 'abi'"))
            return false;
        an = expect(p, T_IDENT, "an abi-return name");
        if (!an)
            return false;
        {
            u8 k;
            bool found = false;

            for (k = IR_ABIRET_SRET; k <= IR_ABIRET_HFA_F128; k++)
                if (tok_is(an, ir_abi_ret_name(k))) {
                    abi_ret = k;
                    found = true;
                }
            if (!found) {
                perr(p, an, "unknown abi-return kind '%.*s'", (int)an->len,
                     an->s);
                return false;
            }
        }
        /* An HFA carries its leaf COUNT: `abi(hfa_f32,3)`. */
        if (abi_ret >= IR_ABIRET_HFA_F32) {
            Tok *n;

            if (!expect(p, T_COMMA, "',' before the HFA leaf count"))
                return false;
            n = expect(p, T_INT, "the HFA leaf count");
            if (!n)
                return false;
            abi_ret_n = (u8)n->ival;
        }
        if (!expect(p, T_RP, "')'"))
            return false;
    }
    if (tok_is(peek(p), "setjmp")) {
        next(p);
        setjmp_marker = true;
    }
    if (tok_is(peek(p), "contract")) {
        next(p);
        contract_marker = true;
    }
    if (!expect(p, T_LB, "'{'"))
        return false;
    f = ir_func_new(p->m, tok_name(p, nm), ret, ptypes, nparams);
    f->variadic = variadic;
    f->unprototyped = unprototyped;
    f->is_weak = fn_weak;
    f->visibility = fn_visibility;
    f->align = fn_align;
    f->is_used = fn_used;
    f->section = fn_section;
    f->is_ctor = fn_ctor;
    f->is_dtor = fn_dtor;
    f->ctor_prio = fn_ctor_prio;
    f->dtor_prio = fn_dtor_prio;
    f->abi_ret = abi_ret;
    f->abi_ret_n = abi_ret_n;
    if (internal_marker)
        f->linkage = IRLINK_INTERNAL;
    f->calls_setjmp = setjmp_marker;
    f->fp_contract = contract_marker;
    if (any_annot) {
        f->param_annots =
            arena_alloc(p->m->arena, nparams * sizeof(u64), _Alignof(u64));
        memcpy(f->param_annots, pannots, nparams * sizeof(u64));
    }
    p->f = f;
    strmap_init(&p->vals);
    strmap_init(&p->blocks);
    p->fixups = NULL;
    p->nfixups = 0;
    p->cap_fixups = 0;
    for (i = 0; i < nparams; i++) {
        def_value(p, pnames[i], f->param_vals[i]);
        if (p->failed)
            goto out;
    }
    if (!prescan_blocks(p))
        goto out;
    if (f->nblocks == 0) {
        perr(p, peek(p), "a function needs at least one block");
        goto out;
    }
    while (peek(p)->kind != T_RB) {
        if (!parse_block_header(p))
            goto out;
        for (;;) {
            Tok *t = peek(p);

            if (t->kind == T_RB)
                break;
            if (t->kind == T_IDENT && t->kind != T_EOF &&
                strmap_get(&p->blocks, t->s, t->len) &&
                peek2(p)->kind == T_LP && lookup_op(t) < 0)
                break; /* next block header */
            if (t->kind == T_EOF) {
                perr(p, t, "missing '}' at end of function");
                goto out;
            }
            if (!parse_inst(p))
                goto out;
        }
    }
    next(p); /* '}' */
    /* Resolve forward value references. */
    for (i = 0; i < p->nfixups; i++) {
        Fixup *fx = &p->fixups[i];
        u32 *hit = strmap_get(&p->vals, fx->name, strlen(fx->name));

        if (!hit) {
            perr(p, &fx->tok, "use of undefined value '%%%s'", fx->name);
            goto out;
        }
        {
            ValueId v = {*hit};
            u64 annot = fx->slot->b; /* keep any call-arg annotation */

            *fx->slot = ir_op_value(f, v);
            fx->slot->b = annot;
        }
    }
out:
    strmap_free(&p->vals);
    strmap_free(&p->blocks);
    p->f = NULL;
    return !p->failed;
}

/* --- globals ------------------------------------------------------------- */

/* `alias @name = @target LINKAGE [weak] [visibility(V)]` */
static bool parse_alias(P *p)
{
    Tok *nm;
    Tok *tg;
    Tok *lk;
    IrAlias *a;
    u8 linkage = IRLINK_EXTERNAL;

    nm = expect_symbol(p, "an alias name");
    if (!nm)
        return false;
    if (!expect(p, T_EQ, "'=' after the alias name"))
        return false;
    tg = expect_symbol(p, "an alias target");
    if (!tg)
        return false;
    lk = next(p);
    if (tok_is(lk, "internal"))
        linkage = IRLINK_INTERNAL;
    else if (tok_is(lk, "external"))
        linkage = IRLINK_EXTERNAL;
    else if (tok_is(lk, "common"))
        linkage = IRLINK_COMMON;
    else {
        perr(p, lk, "expected a linkage (internal/external/common)");
        return false;
    }
    a = ir_alias_new(p->m, tok_name(p, nm), tok_name(p, tg));
    a->linkage = linkage;
    if (tok_is(peek(p), "weak")) {
        next(p);
        a->is_weak = true;
    }
    a->visibility = parse_visibility_suffix(p);
    return true;
}

static bool parse_global(P *p)
{
    Tok *nm = expect_symbol(p, "the global's name");
    Tok *t;
    IrGlobal *g;

    if (!nm)
        return false;
    g = ir_global_new(p->m, tok_name(p, nm));
    t = next(p);
    if (!tok_is(t, "size")) {
        perr(p, t, "expected 'size'");
        return false;
    }
    t = expect(p, T_INT, "the size in bytes");
    if (!t)
        return false;
    g->size = t->ival;
    t = next(p);
    if (!tok_is(t, "align")) {
        perr(p, t, "expected 'align'");
        return false;
    }
    t = expect(p, T_INT, "the alignment");
    if (!t)
        return false;
    if (t->ival > 0xFFFFFFFFull) {
        perr(p, t, "alignment out of range");
        return false;
    }
    g->align = (u32)t->ival;
    t = next(p);
    if (tok_is(t, "internal"))
        g->linkage = IRLINK_INTERNAL;
    else if (tok_is(t, "external"))
        g->linkage = IRLINK_EXTERNAL;
    else if (tok_is(t, "common"))
        g->linkage = IRLINK_COMMON;
    else {
        perr(p, t, "expected a linkage (internal/external/common)");
        return false;
    }
    if (tok_is(peek(p), "tentative")) {
        next(p);
        g->is_tentative = true;
    }
    if (tok_is(peek(p), "tls")) {
        next(p);
        g->is_tls = true;
    }
    if (tok_is(peek(p), "weak")) {
        next(p);
        g->is_weak = true;
    }
    if (tok_is(peek(p), "used")) {
        next(p);
        g->is_used = true;
    }
    if (tok_is(peek(p), "const")) {
        next(p);
        g->is_const = true;
    }
    if (!parse_section_marker(p, &g->section))
        return false;
    g->visibility = parse_visibility_suffix(p);
    if (tok_is(peek(p), "init")) {
        Tok *blob;
        u64 hex_chars;
        u64 i;

        next(p);
        blob = next(p);
        /* The image is one x-prefixed hex ident ("x00ff..") — the prefix
         * keeps a digit-leading blob from lexing as a number. */
        if (blob->kind != T_IDENT || blob->len < 1 || blob->s[0] != 'x') {
            perr(p, blob, "expected an init image: x<hex bytes>");
            return false;
        }
        hex_chars = (u64)blob->len - 1;
        if ((hex_chars & 1) != 0 || hex_chars / 2 != g->size) {
            char needed[21];

            format_twice_u64(needed, g->size);
            perr(p, blob,
                 "initializer has %llu hex chars; size %llu "
                 "needs %s",
                 (unsigned long long)hex_chars, (unsigned long long)g->size,
                 needed);
            return false;
        }
        g->init = arena_alloc(p->arena, g->size ? g->size : 1, 1);
        /* Length equality above proves i * 2 + 2 stays inside blob. */
        for (i = 0; i < g->size; i++) {
            char hc = blob->s[1 + i * 2];
            char lc = blob->s[2 + i * 2];

            if (!is_hex_digit(hc) || !is_hex_digit(lc)) {
                perr(p, blob, "init image contains a non-hex character");
                return false;
            }
            g->init[i] = (u8)(hex_val(hc) << 4 | hex_val(lc));
        }
    }
    if (tok_is(peek(p), "reloc")) {
        u32 cap = 4;
        IrReloc *rl =
            arena_alloc(p->arena, cap * sizeof(IrReloc), _Alignof(IrReloc));

        g->relocs = rl;
        while (tok_is(peek(p), "reloc")) {
            Tok *sym;

            next(p);
            if (g->nrelocs == cap) {
                IrReloc *nr;

                cap *= 2;
                nr = arena_alloc(p->arena, cap * sizeof(IrReloc),
                                 _Alignof(IrReloc));
                memcpy(nr, rl, g->nrelocs * sizeof(IrReloc));
                rl = nr;
                g->relocs = nr;
            }
            t = expect(p, T_INT, "the reloc offset");
            if (!t)
                return false;
            rl[g->nrelocs].offset = t->ival;
            sym = expect_symbol(p, "the reloc symbol");
            if (!sym)
                return false;
            rl[g->nrelocs].symbol = ir_sym(p->m, tok_name(p, sym));
            t = expect(p, T_INT, "the reloc addend");
            if (!t)
                return false;
            rl[g->nrelocs].addend = (i64)t->ival;
            g->nrelocs++;
        }
    }
    return true;
}

/* --- module -------------------------------------------------------------- */

/* Pre-register function names (at brace depth 0) so FUNCREF_INTERNAL can
 * resolve calls to functions defined later in the file. */
static bool prescan_funcs(P *p)
{
    u32 i;
    int depth = 0;
    u32 idx = 0;

    for (i = 0; p->toks[i].kind != T_EOF; i++) {
        if (p->toks[i].kind == T_LB)
            depth++;
        else if (p->toks[i].kind == T_RB)
            depth--;
        else if (depth == 0 && tok_is(&p->toks[i], "func") &&
                 p->toks[i + 1].kind == T_IDENT &&
                 tok_is_symbol(&p->toks[i + 2])) {
            Tok *nm = &p->toks[i + 2];
            const char *name = tok_name(p, nm);
            size_t name_len = strlen(name);

            if (strmap_get(&p->func_ids, name, name_len)) {
                perr(p, nm, "duplicate function '@%.*s'", (int)nm->len, nm->s);
                return false;
            }
            map_put_u32(p, &p->func_ids, name, name_len, ++idx);
        }
    }
    return true;
}

IrModule *ir_parse_module(Arena *arena, DiagCtx *dc, const char *src,
                          const char *path)
{
    P p;
    IrModule *m;

    memset(&p, 0, sizeof(p));
    p.arena = arena;
    p.dc = dc;
    p.file_id = diag_add_file(dc, path, src, strlen(src));
    if (!lex_all(&p, src))
        return NULL;
    m = ir_module_new(arena, dc);
    p.m = m;
    strmap_init(&p.func_ids);
    if (!prescan_funcs(&p))
        goto fail;
    for (;;) {
        Tok *t = peek(&p);

        if (t->kind == T_EOF)
            break;
        if (tok_is(t, "sym")) {
            Tok *nm;
            u32 sym;
            bool weak = false;
            u8 visibility;

            next(&p);
            nm = expect_symbol(&p, "a symbol name");
            if (!nm)
                goto fail;
            sym = ir_sym(m, tok_name(&p, nm));
            if (tok_is(peek(&p), "weak")) {
                next(&p);
                weak = true;
            }
            visibility = parse_visibility_suffix(&p);
            ir_sym_set_attrs(m, sym, weak, visibility);
        } else if (tok_is(t, "alias")) {
            next(&p);
            if (!parse_alias(&p))
                goto fail;
        } else if (tok_is(t, "global")) {
            next(&p);
            if (!parse_global(&p))
                goto fail;
        } else if (tok_is(t, "func")) {
            next(&p);
            if (!parse_func(&p))
                goto fail;
        } else {
            perr(&p, t, "expected 'sym', 'alias', 'global', or 'func'");
            goto fail;
        }
    }
    strmap_free(&p.func_ids);
    return m;
fail:
    strmap_free(&p.func_ids);
    return NULL;
}
