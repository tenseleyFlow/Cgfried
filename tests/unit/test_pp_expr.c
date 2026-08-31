#include <string.h>

#include "pp/pp.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* Harness: lex an expression string into pp-tokens, then evaluate as a
 * #if condition. Error counting is structural via a capture sink. */
typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    int errors;
} ExprFix;

static void count_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    ExprFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
}

static void efix_init(ExprFix *f)
{
    DiagCtx *dc;
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    dc = diag_ctx_new(&f->arena);
    sink.handle = count_sink;
    sink.user = f;
    diag_set_sink(dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, dc, &f->in);
    f->pp.warn = warn_ctx_new(&f->arena, dc);
}

static void efix_free(ExprFix *f)
{
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

static u32 lex_all(ExprFix *f, const char *src, PpToken *out, u32 max)
{
    SourceFile *sf = pp_source_add_buffer(&f->pp, "<expr>", src, strlen(src));
    PpLexer lx;
    u32 n = 0;

    pp_lexer_init(&lx, &f->pp, sf);
    while (n < max && pp_lex_token(&lx, &out[n]))
        n++;
    buf_free(&lx.scratch);
    return n;
}

/* Evaluates `expr`; asserts no errors and the given truth value. */
static void ev(TestCtx *t, const char *expr, bool expect)
{
    ExprFix f;
    PpToken toks[64];
    u32 n;
    bool r;

    efix_init(&f);
    n = lex_all(&f, expr, toks, 64);
    r = pp_eval_condition(&f.pp, toks, n, toks[0].loc);
    if (f.errors != 0)
        t_fail(t, __FILE__, __LINE__, "eval of \"%s\": unexpected error", expr);
    else if (r != expect)
        t_fail(t, __FILE__, __LINE__, "eval of \"%s\": got %d, want %d", expr,
               (int)r, (int)expect);
    t->assertions++;
    efix_free(&f);
}

/* Evaluates `expr`; asserts it diagnoses an error. */
static void ev_err(TestCtx *t, const char *expr)
{
    ExprFix f;
    PpToken toks[64];
    u32 n;

    efix_init(&f);
    n = lex_all(&f, expr, toks, 64);
    (void)pp_eval_condition(&f.pp, toks, n, toks[0].loc);
    if (f.errors == 0)
        t_fail(t, __FILE__, __LINE__, "eval of \"%s\": expected an error",
               expr);
    t->assertions++;
    efix_free(&f);
}

void test_pp_expr_basic(TestCtx *t)
{
    ev(t, "1", true);
    ev(t, "0", false);
    ev(t, "3 > 2", true);
    ev(t, "2 + 2 == 4", true);
    ev(t, "!0 == 1", true);
    ev(t, "(1)", true);
    ev(t, "1 + 2 * 3 == 7", true);
    ev(t, "(1 + 2) * 3 == 9", true);
    ev(t, "0x10 == 16", true);
    ev(t, "010 == 8", true);
    ev(t, "-1 + 1", false);
    ev(t, "~0 == -1", true);
    ev(t, "1 << 4 == 16", true);
    ev(t, "256 >> 4 == 16", true);
    ev(t, "7 / 2 == 3", true);
    ev(t, "7 % 2 == 1", true);
    ev(t, "-7 / 2 == -3", true); /* C truncates toward zero */
    ev(t, "-7 % 2 == -1", true);
}

void test_pp_expr_signedness(TestCtx *t)
{
    /* THE classic: unsigned infection makes -1 huge. */
    ev(t, "-1 < 0u", false);
    ev(t, "-1 > 0u", true);
    ev(t, "-1 < 0", true);
    ev(t, "0u - 1 > 0", true);
    /* Shifts take signedness from the LEFT operand only. */
    ev(t, "-1 >> 1 == -1", true);                 /* arithmetic fill */
    ev(t, "-1 >> 1u == -1", true);                /* right op does not infect */
    ev(t, "0x8000000000000000 >> 63 == 1", true); /* unsigned by magnitude */
    /* Division follows infection. */
    ev(t, "-2 / 2u == 0", false); /* -2/2u is huge/2, not -1 */
    /* Wrap on overflow (pedwarn hook only). */
    ev(t, "0x7FFFFFFFFFFFFFFF + 1 < 0", true);
}

void test_pp_expr_short_circuit(TestCtx *t)
{
    /* Untaken sides are parsed, never evaluated (gcc parity). */
    ev(t, "0 && 1/0", false);
    ev(t, "1 || 1/0", true);
    ev(t, "1 ? 2 : 1/0", true);
    ev(t, "0 ? 1/0 : 2", true);
    ev(t, "2 || 1/0", true);
    ev_err(t, "1/0");
    ev_err(t, "1 % 0");
    ev_err(t, "1 && 1/0");
    ev_err(t, "0 ? 1 : 1/0");
    /* ?: is right-associative. */
    ev(t, "1 ? 0 : 1 ? 1/0 : 1/0", false);
    ev(t, "0 ? 1/0 : 0 ? 1/0 : 5", true);
}

void test_pp_expr_charconst(TestCtx *t)
{
    ev(t, "'A' == 65", true);
    ev(t, "'\\n' == 10", true);
    ev(t, "'\\0' == 0", true);
    ev(t, "'\\x41' == 'A'", true);
    ev(t, "'\\101' == 'A'", true);
    /* Multi-char packs big-endian like gcc; int-typed. */
    ev(t, "'ab' == (('a' << 8) | 'b')", true);
    ev(t, "L'x' == 'x'", true);
    /* Prefixed constants retain their code-unit width in preprocessing
     * expressions. In particular, octal 0400 must not narrow to a zero
     * ordinary byte before #if observes it. */
    ev(t, "L'\\400' == 256", true);
    ev(t, "L'\\x100' == 256", true);
    ev(t, "u'\\400' == 256", true);
    ev(t, "U'\\400' == 256", true);
    /* Keep preprocessing aligned with lex_char_const for the GNU
     * implementation-defined prefixed multichar spelling. */
    ev(t, "L'ab' == 'b'", true);
    ev_err(t, "''");
}

void test_pp_expr_identifiers(TestCtx *t)
{
    /* Surviving identifiers evaluate to 0 — incl. true/false in C17. */
    ev(t, "UNDEFINED_THING", false);
    ev(t, "true", false);
    ev(t, "UNDEF + 1", true);
    ev(t, "defined_but_not == 0", true);
}

void test_pp_expr_errors(TestCtx *t)
{
    ev_err(t, "\"str\"");
    ev_err(t, "1 +");
    ev_err(t, "(1");
    ev_err(t, "1 ? 2");
    ev_err(t, "1 , 2"); /* comma is not allowed in #if */
    ev_err(t, "1 ++ 2");
    ev_err(t, "1.5");    /* floats: invalid integer constant */
    ev_err(t, "0x1e+2"); /* the greedy pp-number, invalid here */
    ev_err(t, "123abc");
    ev_err(t, "1 2"); /* trailing tokens */
    ev_err(t, "");
}

void test_pp_expr_deep_nesting(TestCtx *t)
{
    ev(t, "((((((((((1))))))))))", true);
    ev(t, "1 ? 1 ? 1 ? 1 ? 42 : 0 : 0 : 0 : 0", true);
    ev(t, "0 | 0 ^ 0 & 1 | 1", true);
    ev(t, "(1 | 2 | 4 | 8) == 15", true);
    ev(t, "(0xF0 & 0x1F) == 0x10", true);
    ev(t, "(1 ^ 3) == 2", true);
}
