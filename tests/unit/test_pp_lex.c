#include <string.h>

#include "pp/pp.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* Fixture: preprocessor over an in-memory buffer with an error-counting
 * sink (keeps unit runs quiet; asserts are structural). */
typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    PpLexer lx;
    int errors;
    int warnings;
} LexFix;

static void count_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    LexFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
}

static void fix_init(LexFix *f, const char *src, bool trigraphs)
{
    DiagCtx *dc;
    DiagSink sink;
    SourceFile *sf;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    dc = diag_ctx_new(&f->arena);
    sink.handle = count_sink;
    sink.user = f;
    diag_set_sink(dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, dc, &f->in);
    f->pp.warn = warn_ctx_new(&f->arena, dc);
    f->pp.trigraphs = trigraphs;
    sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    pp_lexer_init(&f->lx, &f->pp, sf);
}

static void fix_free(LexFix *f)
{
    buf_free(&f->lx.scratch);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    arena_free_all(&f->arena);
}

static PpToken next(LexFix *f)
{
    PpToken t;

    pp_lex_token(&f->lx, &t);
    return t;
}

/* Lex one token from src; assert kind and spelling. */
static void one(TestCtx *t, const char *src, PpTokKind kind,
                const char *spelling)
{
    LexFix f;
    PpToken tok;

    fix_init(&f, src, false);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.kind, kind);
    T_ASSERT_EQ_STR(t, tok.spelling, spelling);
    fix_free(&f);
}

void test_pp_lex_ppnum_monster(TestCtx *t)
{
    /* The pp-number grammar is deliberately greedy; conversion validity is
     * Sprint 8's problem. */
    one(t, "0x1e+2", PPTOK_PPNUM, "0x1e+2");
    one(t, "1e+", PPTOK_PPNUM, "1e+");
    one(t, ".5f", PPTOK_PPNUM, ".5f");
    one(t, "0x.8p1", PPTOK_PPNUM, "0x.8p1");
    one(t, "1.2.3.4", PPTOK_PPNUM, "1.2.3.4");
    one(t, "0xE-2", PPTOK_PPNUM, "0xE-2");
    one(t, "123abc", PPTOK_PPNUM, "123abc");
    one(t, "1..2", PPTOK_PPNUM, "1..2");
    one(t, "3e+7", PPTOK_PPNUM, "3e+7");
    one(t, "0x1p-3", PPTOK_PPNUM, "0x1p-3");
    one(t, "1e5", PPTOK_PPNUM, "1e5");
    one(t, "0b101", PPTOK_PPNUM, "0b101");
    one(t, "9007199254740993", PPTOK_PPNUM, "9007199254740993");
    one(t, "1E+x", PPTOK_PPNUM, "1E+x");
    one(t, "0.", PPTOK_PPNUM, "0.");

    /* ".." is two dot punctuators; "..3" is dot then pp-number ".3". */
    {
        LexFix f;
        PpToken a, b;

        fix_init(&f, "..3", false);
        a = next(&f);
        b = next(&f);
        T_ASSERT_EQ_INT(t, a.kind, PPTOK_PUNCT);
        T_ASSERT_EQ_STR(t, a.spelling, ".");
        T_ASSERT_EQ_INT(t, b.kind, PPTOK_PPNUM);
        T_ASSERT_EQ_STR(t, b.spelling, ".3");
        fix_free(&f);
    }
}

void test_pp_lex_splices(TestCtx *t)
{
    LexFix f;
    PpToken tok;
    FileId fid;
    u32 line, col;

    /* One identifier built across three physical lines. */
    fix_init(&f, "ab\\\nc\\\nd rest", false);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.kind, PPTOK_IDENT);
    T_ASSERT_EQ_STR(t, tok.spelling, "abcd");
    pp_loc_resolve(&f.pp.loc, tok.loc, &fid, &line, &col);
    T_ASSERT_EQ_INT(t, line, 1); /* column/line of the first byte */
    T_ASSERT_EQ_INT(t, col, 1);
    tok = next(&f);
    T_ASSERT_EQ_STR(t, tok.spelling, "rest");
    /* The spliced newline is invisible: rest is on logical line 1. */
    T_ASSERT(t, !(tok.flags & PPTOK_F_BOL));
    fix_free(&f);

    /* Backslash-space-newline: warned, spliced anyway (gcc parity). */
    fix_init(&f, "x\\ \ny", false);
    tok = next(&f);
    T_ASSERT_EQ_STR(t, tok.spelling, "xy");
    T_ASSERT_EQ_INT(t, f.warnings, 1);
    fix_free(&f);

    /* A // comment ending in backslash swallows the next line. */
    fix_init(&f, "a// c \\\nswallowed\nz", false);
    tok = next(&f);
    T_ASSERT_EQ_STR(t, tok.spelling, "a");
    tok = next(&f);
    T_ASSERT_EQ_STR(t, tok.spelling, "z");
    T_ASSERT(t, tok.flags & PPTOK_F_BOL);
    fix_free(&f);

    /* Splice inside a string literal. */
    fix_init(&f, "\"a\\\nb\"", false);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.kind, PPTOK_STRLIT);
    T_ASSERT_EQ_STR(t, tok.spelling, "\"ab\"");
    fix_free(&f);
}

void test_pp_lex_flags(TestCtx *t)
{
    LexFix f;
    PpToken a, b, c;

    fix_init(&f, "a b\n c", false);
    a = next(&f);
    b = next(&f);
    c = next(&f);
    T_ASSERT(t, a.flags & PPTOK_F_BOL);
    T_ASSERT(t, !(b.flags & PPTOK_F_BOL));
    T_ASSERT(t, b.flags & PPTOK_F_SPACE);
    T_ASSERT(t, c.flags & PPTOK_F_BOL);
    T_ASSERT(t, c.flags & PPTOK_F_SPACE);
    fix_free(&f);

    /* Comments count as one space. */
    fix_init(&f, "a/*x*/b", false);
    a = next(&f);
    b = next(&f);
    T_ASSERT(t, !(a.flags & PPTOK_F_SPACE));
    T_ASSERT(t, b.flags & PPTOK_F_SPACE);
    fix_free(&f);
}

void test_pp_lex_comment_metadata(TestCtx *t)
{
    LexFix f;
    PpToken a, b;
    const PpComment *c;
    Span sp;

    fix_init(&f, "a /* same */ // fall\\\nthrough\n /* same */ b", false);
    a = next(&f);
    b = next(&f);
    T_ASSERT_EQ_STR(t, a.spelling, "a");
    T_ASSERT_EQ_STR(t, b.spelling, "b");
    T_ASSERT_EQ_INT(t, f.pp.ncomments, 3);
    T_ASSERT_EQ_STR(t, f.pp.comments[0].body, " same ");
    T_ASSERT_EQ_STR(t, f.pp.comments[1].body, " fallthrough");
    T_ASSERT_EQ_STR(t, f.pp.comments[2].body, " same ");
    T_ASSERT(t, f.pp.comments[0].body == f.pp.comments[2].body);

    sp = pp_span(&f.pp, b.loc, b.len);
    c = pp_comment_before(&f.pp, sp);
    T_ASSERT(t, c != NULL);
    T_ASSERT_EQ_STR(t, c->body, " same ");
    sp = pp_span(&f.pp, a.loc, a.len);
    T_ASSERT(t, pp_comment_before(&f.pp, sp) == NULL);
    fix_free(&f);

    /* The end-of-directive probe may scan comments, but it must not create
     * semantic records; the real scan records each physical comment once. */
    fix_init(&f, "x /* probe */\ny", false);
    a = next(&f);
    T_ASSERT(t, pp_lex_at_line_end(&f.lx));
    T_ASSERT_EQ_INT(t, f.pp.ncomments, 0);
    b = next(&f);
    T_ASSERT_EQ_STR(t, b.spelling, "y");
    T_ASSERT_EQ_INT(t, f.pp.ncomments, 1);
    T_ASSERT_EQ_STR(t, f.pp.comments[0].body, " probe ");
    c = pp_comment_before(&f.pp, pp_span(&f.pp, b.loc, b.len));
    T_ASSERT(t, c != NULL);
    T_ASSERT_EQ_STR(t, c->body, " probe ");
    fix_free(&f);
}

void test_pp_lex_literals(TestCtx *t)
{
    LexFix f;
    PpToken tok;

    one(t, "L\"x\"", PPTOK_STRLIT, "L\"x\"");
    one(t, "u8\"y\"", PPTOK_STRLIT, "u8\"y\"");
    one(t, "u'c'", PPTOK_CHARCONST, "u'c'");
    one(t, "'q'", PPTOK_CHARCONST, "'q'");
    one(t, "\"esc\\\"q\"", PPTOK_STRLIT, "\"esc\\\"q\"");
    /* Not a prefix: whole identifier isn't L/u/U/u8. */
    {
        fix_init(&f, "abcL\"x\"", false);
        tok = next(&f);
        T_ASSERT_EQ_INT(t, tok.kind, PPTOK_IDENT);
        T_ASSERT_EQ_STR(t, tok.spelling, "abcL");
        tok = next(&f);
        T_ASSERT_EQ_INT(t, tok.kind, PPTOK_STRLIT);
        fix_free(&f);
    }
    /* Unterminated literal: error, never scans past the logical newline. */
    {
        fix_init(&f, "\"abc\nz", false);
        tok = next(&f);
        T_ASSERT_EQ_INT(t, tok.kind, PPTOK_STRLIT);
        T_ASSERT_EQ_INT(t, f.errors, 1);
        tok = next(&f);
        T_ASSERT_EQ_STR(t, tok.spelling, "z");
        fix_free(&f);
    }
    /* Unterminated block comment at EOF. */
    {
        fix_init(&f, "/* x", false);
        tok = next(&f);
        T_ASSERT_EQ_INT(t, tok.kind, PPTOK_EOF);
        T_ASSERT_EQ_INT(t, f.errors, 1);
        fix_free(&f);
    }
}

void test_pp_lex_puncts(TestCtx *t)
{
    LexFix f;
    PpToken tok;

    one(t, ">>=", PPTOK_PUNCT, ">>=");
    one(t, "...", PPTOK_PUNCT, "...");
    one(t, "%:%:", PPTOK_PUNCT, "%:%:");

    /* Digraphs map to primary punct values but keep their spelling. */
    fix_init(&f, "<% %> <: :> %:", false);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_LBRACE);
    T_ASSERT_EQ_STR(t, tok.spelling, "<%");
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_RBRACE);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_LBRACKET);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_RBRACKET);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_HASH);
    T_ASSERT_EQ_STR(t, tok.spelling, "%:");
    fix_free(&f);

    /* Max munch: >>> is >> then >. */
    fix_init(&f, ">>>", false);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_SHR);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_GT);
    fix_free(&f);
}

void test_pp_lex_misc(TestCtx *t)
{
    LexFix f;
    PpToken a, b;

    /* Stray bytes become PPTOK_OTHER (an error only at phase 7). */
    one(t, "@", PPTOK_OTHER, "@");
    one(t, "`", PPTOK_OTHER, "`");

    /* UCN-initial identifier. */
    one(t, "\\u0041bc", PPTOK_IDENT, "\\u0041bc");

    /* UTF-8 identifier bytes pass through transparently. */
    one(t, "caf\xc3\xa9", PPTOK_IDENT, "caf\xc3\xa9");

    /* Interning: identical spellings share one pointer. */
    fix_init(&f, "foo foo", false);
    a = next(&f);
    b = next(&f);
    T_ASSERT(t, a.spelling == b.spelling);
    fix_free(&f);

    /* Header-names exist only via the dedicated entry point. */
    fix_init(&f, "<stdio.h> \"x.h\"", false);
    T_ASSERT(t, pp_lex_header_name(&f.lx, &a));
    T_ASSERT_EQ_INT(t, a.kind, PPTOK_HEADER_NAME);
    T_ASSERT_EQ_STR(t, a.spelling, "<stdio.h>");
    T_ASSERT(t, pp_lex_header_name(&f.lx, &b));
    T_ASSERT_EQ_STR(t, b.spelling, "\"x.h\"");
    fix_free(&f);
}

void test_pp_lex_trigraphs(TestCtx *t)
{
    LexFix f;
    PpToken tok;

    /* Off by default: ??= is ? ? = (three tokens). */
    fix_init(&f, "?\?=", false);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_QUESTION);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_QUESTION);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_ASSIGN);
    fix_free(&f);

    /* On: ??= is #; ??/ at line end splices. */
    fix_init(&f, "?\?=", true);
    tok = next(&f);
    T_ASSERT_EQ_INT(t, tok.punct, PUNCT_HASH);
    fix_free(&f);

    fix_init(&f, "ab?\?/\ncd", true);
    tok = next(&f);
    T_ASSERT_EQ_STR(t, tok.spelling, "abcd");
    fix_free(&f);
}
