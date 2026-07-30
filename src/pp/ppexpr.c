#include <string.h>

#include "pp/pp.h"

/* #if/#elif constant expressions: exactly 64-bit target intmax_t/uintmax_t
 * arithmetic, usual unsigned infection, short-circuiting && || ?: whose
 * untaken sides are parsed but never evaluated (gcc parity: 1 ? 2 : 1/0 is
 * fine, 1/0 is an error). */

typedef struct PpVal {
    u64 v;
    bool is_unsigned;
} PpVal;

typedef struct {
    Preprocessor *pp;
    const PpToken *toks;
    u32 n;
    u32 pos;
    SrcLoc line_loc;
    bool failed;
} EvalCtx;

static PpVal make_val(u64 v, bool is_unsigned)
{
    PpVal r;

    r.v = v;
    r.is_unsigned = is_unsigned;
    return r;
}

static const PpToken *peek_tok(EvalCtx *c)
{
    return c->pos < c->n ? &c->toks[c->pos] : NULL;
}

static void eval_error(EvalCtx *c, const PpToken *at, const char *msg)
{
    if (c->failed)
        return;
    c->failed = true;
    if (at)
        pp_diag_at(c->pp, DIAG_ERROR, at->loc, at->len, "%s", msg);
    else
        pp_diag_at(c->pp, DIAG_ERROR, c->line_loc, 1, "%s", msg);
}

static bool accept_punct(EvalCtx *c, PpPunct p)
{
    const PpToken *t = peek_tok(c);

    if (t && t->kind == PPTOK_PUNCT && t->punct == p) {
        c->pos++;
        return true;
    }
    return false;
}

/* pp-number -> value. Decimal/octal/hex with uUlL suffixes; anything the
 * greedy pp-number grammar swallowed that is not a valid integer constant
 * (floats, 0x1e+2, bad suffixes) is an error here. */
static PpVal parse_number(EvalCtx *c, const PpToken *t)
{
    const char *s = t->spelling;
    size_t len = t->len, i = 0;
    u64 v = 0;
    int base = 10;
    bool suf_u = false;
    bool overflow = false;

    if (len > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        i = 2;
    } else if (s[0] == '0' && len > 1) {
        base = 8;
        i = 1;
    }

    for (; i < len; i++) {
        int d;
        char ch = s[i];

        if (ch >= '0' && ch <= '9')
            d = ch - '0';
        else if (base == 16 && ch >= 'a' && ch <= 'f')
            d = ch - 'a' + 10;
        else if (base == 16 && ch >= 'A' && ch <= 'F')
            d = ch - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        if (v > (~(u64)0 - (u64)d) / (u64)base)
            overflow = true;
        v = v * (u64)base + (u64)d;
    }

    /* Suffixes: u/U once, l/L or ll/LL once, any order. */
    {
        bool suf_l = false;
        while (i < len) {
            char ch = s[i];
            if ((ch == 'u' || ch == 'U') && !suf_u) {
                suf_u = true;
                i++;
            } else if ((ch == 'l' || ch == 'L') && !suf_l) {
                suf_l = true;
                i++;
                if (i < len && s[i] == ch)
                    i++; /* ll / LL (mixed lL rejected below) */
            } else {
                break;
            }
        }
        if (i != len) {
            eval_error(c, t,
                       "invalid integer constant in preprocessor "
                       "expression");
            return make_val(0, false);
        }
    }
    if (overflow)
        eval_error(c, t, "integer constant is too large for uintmax_t");

    /* In #if arithmetic everything is intmax_t/uintmax_t; a decimal that
     * does not fit intmax_t is unsigned per the usual ladder collapse. */
    return make_val(v, suf_u || v > 0x7FFFFFFFFFFFFFFFull);
}

/* Character constant in the TARGET execution charset (identity for our
 * targets). Multi-char packs big-endian like gcc: 'ab' == ('a'<<8)|'b'. */
static PpVal parse_charconst(EvalCtx *c, const PpToken *t)
{
    const char *s = t->spelling;
    size_t len = t->len, i = 0;
    u64 v = 0;
    int nchars = 0;

    /* Skip encoding prefix; value semantics identical for our targets. */
    if (s[i] == 'L' || s[i] == 'u' || s[i] == 'U')
        i++;
    if (i >= len || s[i] != '\'')
        goto bad;
    i++;
    while (i < len && s[i] != '\'') {
        u64 ch;
        if (s[i] == '\\') {
            i++;
            if (i >= len)
                goto bad;
            switch (s[i]) {
            case 'n':
                ch = '\n';
                i++;
                break;
            case 't':
                ch = '\t';
                i++;
                break;
            case 'r':
                ch = '\r';
                i++;
                break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7': {
                int k = 0;
                ch = 0;
                while (k < 3 && i < len && s[i] >= '0' && s[i] <= '7') {
                    ch = ch * 8 + (u64)(s[i] - '0');
                    i++;
                    k++;
                }
                break;
            }
            case 'x': {
                i++;
                ch = 0;
                while (i < len && ((s[i] >= '0' && s[i] <= '9') ||
                                   (s[i] >= 'a' && s[i] <= 'f') ||
                                   (s[i] >= 'A' && s[i] <= 'F'))) {
                    int d = (s[i] >= '0' && s[i] <= '9') ? s[i] - '0'
                            : (s[i] >= 'a')              ? s[i] - 'a' + 10
                                                         : s[i] - 'A' + 10;
                    ch = ch * 16 + (u64)d;
                    i++;
                }
                break;
            }
            case '\\':
                ch = '\\';
                i++;
                break;
            case '\'':
                ch = '\'';
                i++;
                break;
            case '"':
                ch = '"';
                i++;
                break;
            case 'a':
                ch = '\a';
                i++;
                break;
            case 'b':
                ch = '\b';
                i++;
                break;
            case 'f':
                ch = '\f';
                i++;
                break;
            case 'v':
                ch = '\v';
                i++;
                break;
            default:
                eval_error(c, t,
                           "unknown escape sequence in character "
                           "constant");
                return make_val(0, false);
            }
        } else {
            ch = (u64)(unsigned char)s[i];
            i++;
        }
        v = (v << 8) | (ch & 0xFF);
        nchars++;
    }
    if (i >= len || s[i] != '\'' || nchars == 0)
        goto bad;
    if (nchars > 1) {
        /* gcc pedwarns on multi-char; hook is Sprint 37, value matches. */
        if (nchars > 8) {
            eval_error(c, t, "character constant too long");
            return make_val(0, false);
        }
        return make_val(v, false);
    }
    /* Single char: int-typed, sign per target `char` (signed on
     * x86_64-linux-gnu; per-target charsign threading arrives with sema). */
    return make_val((u64)(i64)(i8)(u8)v, false);
bad:
    eval_error(c, t, "invalid character constant");
    return make_val(0, false);
}

/* Grammar (low to high): ?: || && | ^ & (== !=) (< > <= >=) (<< >>) (+ -)
 * (* / %) unary primary. `live` gates evaluation-only effects (division by
 * zero) on the taken path. */
static PpVal eval_cond(EvalCtx *c, bool live);

static PpVal eval_primary(EvalCtx *c, bool live)
{
    const PpToken *t = peek_tok(c);

    if (c->failed)
        return make_val(0, false);
    if (!t) {
        eval_error(c, NULL, "expected expression in #if");
        return make_val(0, false);
    }
    if (accept_punct(c, PUNCT_LPAREN)) {
        PpVal v = eval_cond(c, live);
        if (!accept_punct(c, PUNCT_RPAREN))
            eval_error(c, peek_tok(c), "expected ')' in #if expression");
        return v;
    }
    c->pos++;
    switch ((PpTokKind)t->kind) {
    case PPTOK_PPNUM:
        return parse_number(c, t);
    case PPTOK_CHARCONST:
        return parse_charconst(c, t);
    case PPTOK_IDENT:
        /* Surviving identifiers (incl. true/false pre-C23) are 0. The
         * -Wundef hook lands with Sprint 37's machinery (gcc default:
         * off), so no diagnostic today. */
        return make_val(0, false);
    case PPTOK_STRLIT:
        eval_error(c, t, "string literals are not allowed in #if");
        return make_val(0, false);
    case PPTOK_PUNCT:
    case PPTOK_HEADER_NAME:
    case PPTOK_OTHER:
    case PPTOK_PLACEMARKER:
    case PPTOK_EOF:
        break;
    }
    eval_error(c, t, "unexpected token in #if expression");
    return make_val(0, false);
}

static PpVal eval_unary(EvalCtx *c, bool live)
{
    if (accept_punct(c, PUNCT_BANG)) {
        PpVal v = eval_unary(c, live);
        return make_val(v.v == 0 ? 1 : 0, false); /* ! yields signed int */
    }
    if (accept_punct(c, PUNCT_TILDE)) {
        PpVal v = eval_unary(c, live);
        return make_val(~v.v, v.is_unsigned);
    }
    if (accept_punct(c, PUNCT_PLUS))
        return eval_unary(c, live);
    if (accept_punct(c, PUNCT_MINUS)) {
        PpVal v = eval_unary(c, live);
        return make_val(0 - v.v, v.is_unsigned); /* wraps; stays unsigned */
    }
    if (accept_punct(c, PUNCT_PLUSPLUS) || accept_punct(c, PUNCT_MINUSMINUS)) {
        eval_error(c, &c->toks[c->pos - 1], "'++'/'--' are not allowed in #if");
        return make_val(0, false);
    }
    return eval_primary(c, live);
}

/* Signed comparison/arithmetic helpers on the 64-bit representation. */
static bool s_lt(u64 a, u64 b)
{
    return (i64)a < (i64)b;
}

static PpVal eval_mul(EvalCtx *c, bool live)
{
    PpVal l = eval_unary(c, live);

    for (;;) {
        bool is_div = false, is_mod = false;
        PpVal r;
        bool uns;

        if (accept_punct(c, PUNCT_STAR))
            ;
        else if (accept_punct(c, PUNCT_SLASH))
            is_div = true;
        else if (accept_punct(c, PUNCT_PERCENT))
            is_mod = true;
        else
            return l;
        r = eval_unary(c, live);
        if (c->failed)
            return make_val(0, false);
        uns = l.is_unsigned || r.is_unsigned;
        if (is_div || is_mod) {
            if (r.v == 0) {
                /* Only an error on the LIVE path: the untaken side of
                 * && || ?: is parsed, never evaluated (gcc parity). */
                if (live)
                    eval_error(c, &c->toks[c->pos - 1],
                               "division by zero in #if expression");
                l = make_val(0, uns);
                continue;
            }
            if (uns)
                l = make_val(is_div ? l.v / r.v : l.v % r.v, true);
            else if ((i64)r.v == -1 && (i64)l.v == (i64)(1ull << 63))
                l = make_val(is_div ? l.v : 0, false); /* wrap, no trap */
            else
                l = make_val(is_div ? (u64)((i64)l.v / (i64)r.v)
                                    : (u64)((i64)l.v % (i64)r.v),
                             false);
        } else {
            l = make_val(l.v * r.v, uns); /* wraps 2's-complement */
        }
    }
}

static PpVal eval_add(EvalCtx *c, bool live)
{
    PpVal l = eval_mul(c, live);

    for (;;) {
        bool minus;
        PpVal r;

        if (accept_punct(c, PUNCT_PLUS))
            minus = false;
        else if (accept_punct(c, PUNCT_MINUS))
            minus = true;
        else
            return l;
        r = eval_mul(c, live);
        l = make_val(minus ? l.v - r.v : l.v + r.v,
                     l.is_unsigned || r.is_unsigned);
    }
}

static PpVal eval_shift(EvalCtx *c, bool live)
{
    PpVal l = eval_add(c, live);

    for (;;) {
        bool right;
        PpVal r;
        u64 cnt;

        if (accept_punct(c, PUNCT_SHL))
            right = false;
        else if (accept_punct(c, PUNCT_SHR))
            right = true;
        else
            return l;
        r = eval_add(c, live);
        cnt = r.v;
        /* Signedness from the LEFT operand only (gcc parity). Counts >= 64
         * (or negative signed counts) produce 0 / sign-fill like gcc. */
        if (!r.is_unsigned && s_lt(cnt, 0))
            cnt = 64;
        if (cnt >= 64) {
            if (right && !l.is_unsigned && s_lt(l.v, 0))
                l = make_val(~(u64)0, l.is_unsigned);
            else
                l = make_val(0, l.is_unsigned);
        } else if (right) {
            if (!l.is_unsigned && s_lt(l.v, 0))
                l = make_val(~((~l.v) >> cnt), false); /* arithmetic fill */
            else
                l = make_val(l.v >> cnt, l.is_unsigned);
        } else {
            l = make_val(l.v << cnt, l.is_unsigned);
        }
    }
}

static PpVal eval_rel(EvalCtx *c, bool live)
{
    PpVal l = eval_shift(c, live);

    for (;;) {
        PpPunct op;
        PpVal r;
        bool uns, res;

        if (accept_punct(c, PUNCT_LT))
            op = PUNCT_LT;
        else if (accept_punct(c, PUNCT_GT))
            op = PUNCT_GT;
        else if (accept_punct(c, PUNCT_LE))
            op = PUNCT_LE;
        else if (accept_punct(c, PUNCT_GE))
            op = PUNCT_GE;
        else
            return l;
        r = eval_shift(c, live);
        uns = l.is_unsigned || r.is_unsigned;
        if (uns) {
            res = op == PUNCT_LT   ? l.v < r.v
                  : op == PUNCT_GT ? l.v > r.v
                  : op == PUNCT_LE ? l.v <= r.v
                                   : l.v >= r.v;
        } else {
            res = op == PUNCT_LT   ? s_lt(l.v, r.v)
                  : op == PUNCT_GT ? s_lt(r.v, l.v)
                  : op == PUNCT_LE ? !s_lt(r.v, l.v)
                                   : !s_lt(l.v, r.v);
        }
        l = make_val(res ? 1 : 0, false);
    }
}

static PpVal eval_eq(EvalCtx *c, bool live)
{
    PpVal l = eval_rel(c, live);

    for (;;) {
        bool ne;
        PpVal r;

        if (accept_punct(c, PUNCT_EQEQ))
            ne = false;
        else if (accept_punct(c, PUNCT_NOTEQ))
            ne = true;
        else
            return l;
        r = eval_rel(c, live);
        l = make_val((l.v == r.v) != ne ? 1 : 0, false);
    }
}

static PpVal eval_bitand(EvalCtx *c, bool live)
{
    PpVal l = eval_eq(c, live);

    while (peek_tok(c) && peek_tok(c)->kind == PPTOK_PUNCT &&
           peek_tok(c)->punct == PUNCT_AMP) {
        PpVal r;
        c->pos++;
        r = eval_eq(c, live);
        l = make_val(l.v & r.v, l.is_unsigned || r.is_unsigned);
    }
    return l;
}

static PpVal eval_bitxor(EvalCtx *c, bool live)
{
    PpVal l = eval_bitand(c, live);

    while (accept_punct(c, PUNCT_CARET)) {
        PpVal r = eval_bitand(c, live);
        l = make_val(l.v ^ r.v, l.is_unsigned || r.is_unsigned);
    }
    return l;
}

static PpVal eval_bitor(EvalCtx *c, bool live)
{
    PpVal l = eval_bitxor(c, live);

    while (accept_punct(c, PUNCT_PIPE)) {
        PpVal r = eval_bitxor(c, live);
        l = make_val(l.v | r.v, l.is_unsigned || r.is_unsigned);
    }
    return l;
}

static PpVal eval_and(EvalCtx *c, bool live)
{
    PpVal l = eval_bitor(c, live);

    while (accept_punct(c, PUNCT_AMPAMP)) {
        bool right_live = live && l.v != 0;
        PpVal r = eval_bitor(c, right_live);
        l = make_val((l.v != 0 && r.v != 0) ? 1 : 0, false);
    }
    return l;
}

static PpVal eval_or(EvalCtx *c, bool live)
{
    PpVal l = eval_and(c, live);

    while (accept_punct(c, PUNCT_PIPEPIPE)) {
        bool right_live = live && l.v == 0;
        PpVal r = eval_and(c, right_live);
        l = make_val((l.v != 0 || r.v != 0) ? 1 : 0, false);
    }
    return l;
}

static PpVal eval_cond(EvalCtx *c, bool live)
{
    PpVal cond = eval_or(c, live);

    if (accept_punct(c, PUNCT_QUESTION)) {
        /* Right-associative; both arms parsed, only the taken arm live. */
        PpVal a, b;
        a = eval_cond(c, live && cond.v != 0);
        if (!accept_punct(c, PUNCT_COLON)) {
            eval_error(c, peek_tok(c), "expected ':' in #if expression");
            return make_val(0, false);
        }
        b = eval_cond(c, live && cond.v == 0);
        return make_val(cond.v != 0 ? a.v : b.v,
                        a.is_unsigned || b.is_unsigned);
    }
    return cond;
}

/* Replace `defined X` / `defined ( X )` BEFORE the expansion seam. A
 * `defined` produced BY expansion is UB (6.10.1p4); we match gcc and
 * evaluate it as `defined` with no diagnostic (see the sprint file). */
static u32 replace_defined(Preprocessor *pp, const PpToken *in, u32 n,
                           PpToken *out, SrcLoc line_loc, bool *ok)
{
    u32 i = 0, o = 0;

    *ok = true;
    while (i < n) {
        if (in[i].kind == PPTOK_IDENT &&
            strcmp(in[i].spelling, "defined") == 0) {
            const char *name = NULL;
            SrcLoc loc = in[i].loc;
            if (i + 1 < n && in[i + 1].kind == PPTOK_IDENT) {
                name = in[i + 1].spelling;
                i += 2;
            } else if (i + 3 < n && in[i + 1].kind == PPTOK_PUNCT &&
                       in[i + 1].punct == PUNCT_LPAREN &&
                       in[i + 2].kind == PPTOK_IDENT &&
                       in[i + 3].kind == PPTOK_PUNCT &&
                       in[i + 3].punct == PUNCT_RPAREN) {
                name = in[i + 2].spelling;
                i += 4;
            } else {
                pp_diag_at(pp, DIAG_ERROR, loc, in[i].len,
                           "operator 'defined' requires an identifier");
                *ok = false;
                return 0;
            }
            memset(&out[o], 0, sizeof(PpToken));
            out[o].kind = PPTOK_PPNUM;
            out[o].spelling = pp_macro_lookup(pp, name) ? "1" : "0";
            out[o].len = 1;
            out[o].loc = loc;
            o++;
        } else {
            out[o++] = in[i++];
        }
    }
    (void)line_loc;
    return o;
}

bool pp_eval_condition(Preprocessor *pp, const PpToken *toks, u32 n, SrcLoc loc)
{
    PpToken *scratch;
    u32 sn;
    bool ok;
    EvalCtx c;
    PpVal v;

    if (n == 0) {
        pp_diag_at(pp, DIAG_ERROR, loc, 1, "#if with no expression");
        return false;
    }

    scratch = arena_alloc(pp->arena, n * sizeof(PpToken), _Alignof(PpToken));
    sn = replace_defined(pp, toks, n, scratch, loc, &ok);
    if (!ok)
        return false;
    {
        /* Real expansion; then one more defined-replacement pass for
         * `defined` produced BY expansion (UB per 6.10.1p4 — match gcc:
         * evaluate it as the operator, no diagnostic). */
        PpToken *ex;
        u32 en;
        pp->in_if_line = true;
        en = pp_expand_list(pp, scratch, sn, &ex);
        pp->in_if_line = false;
        scratch = arena_alloc(pp->arena, (en ? en : 1) * sizeof(PpToken),
                              _Alignof(PpToken));
        sn = replace_defined(pp, ex, en, scratch, loc, &ok);
        if (!ok)
            return false;
    }

    memset(&c, 0, sizeof(c));
    c.pp = pp;
    c.toks = scratch;
    c.n = sn;
    c.line_loc = loc;
    v = eval_cond(&c, true);
    if (!c.failed && c.pos != c.n)
        eval_error(&c, &c.toks[c.pos],
                   c.toks[c.pos].kind == PPTOK_PUNCT &&
                           c.toks[c.pos].punct == PUNCT_COMMA
                       ? "',' is not allowed in #if expressions"
                       : "trailing tokens after #if expression");
    if (c.failed)
        return false;
    return v.v != 0;
}
