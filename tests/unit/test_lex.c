#include <string.h>

#include "lex/lex.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* Lexer units: the keyword table across all six -std modes, the C11
 * 6.4.4.1 integer ladder, escape/UCN torture, and the phase-6 prefix
 * matrix. Everything runs through the real pp so spans stay honest. */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
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

static void lfix_init(LexFix *f)
{
    DiagCtx *dc;
    DiagSink s;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    dc = diag_ctx_new(&f->arena);
    s.handle = count_sink;
    s.user = f;
    diag_set_sink(dc, s);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, dc, &f->in);
}

static void lfix_free(LexFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

VEC_DECL(PpVecU, PpToken);

/* Lexes `src` under `std` and `target` and returns the token list. */
static TokenList lex_src_target(LexFix *f, const char *src, CStd std,
                                TargetKind target_kind)
{
    SourceFile *sf;
    PpVecU pv = {NULL, 0, 0};
    LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec target;

    lfix_init(f);
    memset(&lang, 0, sizeof(lang));
    lang.std = std;
    lang.gnu_mode = std >= STD_GNU89;
    lang.warnings = warn_ctx_new(&f->arena, f->pp.diag);
    f->pp.warn = lang.warnings;
    target.kind = target_kind;

    sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t))
        PpVecU_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, target, &f->arena);
    PpVecU_free(&pv);
    return tl;
}

static TokenList lex_src(LexFix *f, const char *src, CStd std)
{
    return lex_src_target(f, src, std, CGF_TARGET_X86_64_LINUX_GNU);
}

void test_lex_keyword_std_modes(TestCtx *t)
{
    static const struct {
        CStd std;
        const char *name;
        bool inline_kw, restrict_kw, bool_kw, atomic_kw, typeof_kw;
    } modes[] = {
        /* c89: inline/restrict/_Bool are ORDINARY IDENTIFIERS —
         * `int inline = 1;` must compile. */
        {STD_C89, "c89", false, false, false, false, false},
        {STD_GNU89, "gnu89", true, false, false, false, true},
        {STD_C99, "c99", true, true, true, false, false},
        {STD_C11, "c11", true, true, true, true, false},
        {STD_C17, "c17", true, true, true, true, false},
        {STD_GNU17, "gnu17", true, true, true, true, true},
    };
    size_t m;

    for (m = 0; m < CGF_ARRAY_LEN(modes); m++) {
        LangOpts lang;

        memset(&lang, 0, sizeof(lang));
        lang.std = modes[m].std;
        lang.gnu_mode = modes[m].std >= STD_GNU89;

        T_ASSERT_EQ_INT(t, lex_keyword_lookup("inline", &lang) != KW_NONE,
                        modes[m].inline_kw);
        T_ASSERT_EQ_INT(t, lex_keyword_lookup("restrict", &lang) != KW_NONE,
                        modes[m].restrict_kw);
        T_ASSERT_EQ_INT(t, lex_keyword_lookup("_Bool", &lang) != KW_NONE,
                        modes[m].bool_kw);
        T_ASSERT_EQ_INT(t, lex_keyword_lookup("_Atomic", &lang) != KW_NONE,
                        modes[m].atomic_kw);
        T_ASSERT_EQ_INT(t, lex_keyword_lookup("typeof", &lang) != KW_NONE,
                        modes[m].typeof_kw);
        T_ASSERT(t, lex_keyword_lookup("_Float32", &lang) == KW_FLOAT32);
        T_ASSERT(t, lex_keyword_lookup("_Float64", &lang) == KW_FLOAT64);
        T_ASSERT(t, lex_keyword_lookup("_Float32x", &lang) == KW_FLOAT32X);
        T_ASSERT(t, lex_keyword_lookup("_Float64x", &lang) == KW_FLOAT64X);
        /* C89 core and the __-spelled variants are keywords everywhere. */
        T_ASSERT(t, lex_keyword_lookup("int", &lang) == KW_INT);
        T_ASSERT(t, lex_keyword_lookup("__typeof__", &lang) != KW_NONE);
        /* Naming the keyword, not using it. check_bans allow */
        T_ASSERT(t, lex_keyword_lookup("__attr"
                                       "ibute__",
                                       &lang) != KW_NONE);
        T_ASSERT(t, lex_keyword_lookup("notakeyword", &lang) == KW_NONE);
    }
}

void test_lex_c89_inline_is_identifier(TestCtx *t)
{
    LexFix f;
    TokenList tl = lex_src(&f, "int inline = 1;\n", STD_C89);

    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tl.n >= 2);
    T_ASSERT(t, tl.toks[0].kind == TOK_KEYWORD); /* int */
    T_ASSERT(t, tl.toks[1].kind == TOK_IDENT);   /* inline */
    T_ASSERT_EQ_STR(t, tl.toks[1].spelling, "inline");
    lfix_free(&f);

    /* ...and a keyword in c17. */
    tl = lex_src(&f, "int inline = 1;\n", STD_C17);
    T_ASSERT(t, tl.toks[1].kind == TOK_KEYWORD);
    lfix_free(&f);
}

/* One row of the ladder: source text -> (value, classified type). */
static void ladder(TestCtx *t, const char *src, u64 want_val,
                   IntConstType want_ty)
{
    LexFix f;
    TokenList tl = lex_src(&f, src, STD_C17);

    if (f.errors != 0) {
        t_fail(t, __FILE__, __LINE__, "%s: unexpected error", src);
    } else if (tl.n < 1 || tl.toks[0].kind != TOK_INT_CONST) {
        t_fail(t, __FILE__, __LINE__, "%s: not an integer constant", src);
    } else if (tl.toks[0].int_val != want_val) {
        t_fail(t, __FILE__, __LINE__, "%s: value %llu, want %llu", src,
               (unsigned long long)tl.toks[0].int_val,
               (unsigned long long)want_val);
    } else if (tl.toks[0].int_type != want_ty) {
        t_fail(t, __FILE__, __LINE__, "%s: type %s, want %s", src,
               lex_int_type_name((IntConstType)tl.toks[0].int_type),
               lex_int_type_name(want_ty));
    }
    t->assertions++;
    lfix_free(&f);
}

void test_lex_int_ladder(TestCtx *t)
{
    /* Unsuffixed decimal: SIGNED rungs only. */
    ladder(t, "0", 0, ITY_INT);
    ladder(t, "42", 42, ITY_INT);
    ladder(t, "2147483647", 2147483647ull, ITY_INT);
    ladder(t, "2147483648", 2147483648ull, ITY_LONG);
    ladder(t, "4294967295", 4294967295ull, ITY_LONG);
    ladder(t, "9223372036854775807", 9223372036854775807ull, ITY_LONG);

    /* THE ASYMMETRY: hex may land on unsigned at each rung. */
    ladder(t, "0x7fffffff", 0x7fffffffull, ITY_INT);
    ladder(t, "0x80000000", 0x80000000ull, ITY_UINT);
    ladder(t, "0xffffffff", 0xffffffffull, ITY_UINT);
    ladder(t, "0x100000000", 0x100000000ull, ITY_LONG);
    ladder(t, "0x8000000000000000", 0x8000000000000000ull, ITY_ULONG);
    ladder(t, "0xffffffffffffffff", 0xffffffffffffffffull, ITY_ULONG);

    /* Octal follows the hex ladder. */
    ladder(t, "0777", 0777, ITY_INT);
    ladder(t, "020000000000", 020000000000ull, ITY_UINT);

    /* GNU binary constants follow the same non-decimal ladder. */
    ladder(t, "0b101010", 42, ITY_INT);
    ladder(t, "0B10000000000000000000000000000000", 0x80000000ull, ITY_UINT);
    ladder(t, "0b111u", 7, ITY_UINT);

    /* Suffixes, any order/case. */
    ladder(t, "1u", 1, ITY_UINT);
    ladder(t, "1U", 1, ITY_UINT);
    ladder(t, "1l", 1, ITY_LONG);
    ladder(t, "1L", 1, ITY_LONG);
    ladder(t, "1ul", 1, ITY_ULONG);
    ladder(t, "1lu", 1, ITY_ULONG);
    ladder(t, "1LU", 1, ITY_ULONG);
    ladder(t, "1ll", 1, ITY_LLONG);
    ladder(t, "1LL", 1, ITY_LLONG);
    ladder(t, "1ull", 1, ITY_ULLONG);
    ladder(t, "1llu", 1, ITY_ULLONG);
    ladder(t, "4294967295u", 4294967295ull, ITY_UINT);
    ladder(t, "2147483648u", 2147483648ull, ITY_UINT);
}

/* Expects at least one error from lexing `src`. */
static void lex_err(TestCtx *t, const char *src, CStd std)
{
    LexFix f;

    (void)lex_src(&f, src, std);
    if (f.errors == 0)
        t_fail(t, __FILE__, __LINE__, "%s: expected an error", src);
    t->assertions++;
    lfix_free(&f);
}

void test_lex_int_errors(TestCtx *t)
{
    lex_err(t, "08\n", STD_C17);  /* malformed octal, not two tokens */
    lex_err(t, "1lL\n", STD_C17); /* mixed-case ll is not a suffix */
    lex_err(t, "1Ll\n", STD_C17);
    lex_err(t, "1uu\n", STD_C17);
    lex_err(t, "1z\n", STD_C17);
    lex_err(t, "0x\n", STD_C17); /* hex with no digits */
    lex_err(t, "0b\n", STD_C17); /* binary with no digits */
    lex_err(t, "0b102\n", STD_C17);
}

/* Character-constant value check. */
static void chr(TestCtx *t, const char *src, i64 want)
{
    LexFix f;
    TokenList tl = lex_src(&f, src, STD_C17);

    if (f.errors != 0)
        t_fail(t, __FILE__, __LINE__, "%s: unexpected error", src);
    else if (tl.toks[0].kind != TOK_CHAR_CONST)
        t_fail(t, __FILE__, __LINE__, "%s: not a char constant", src);
    else if ((i64)tl.toks[0].int_val != want)
        t_fail(t, __FILE__, __LINE__, "%s: value %lld, want %lld", src,
               (long long)tl.toks[0].int_val, (long long)want);
    /* A character constant has type INT in C, never char. */
    else if (tl.toks[0].int_type != ITY_INT)
        t_fail(t, __FILE__, __LINE__, "%s: type is not int", src);
    t->assertions++;
    lfix_free(&f);
}

void test_lex_char_consts(TestCtx *t)
{
    chr(t, "'a'\n", 97);
    chr(t, "'\\n'\n", 10);
    chr(t, "'\\0'\n", 0);
    chr(t, "'\\x41'\n", 65);
    chr(t, "'\\101'\n", 65);
    /* Octal takes at most THREE digits: '\1234' is '\123' then '4', a
     * multi-char constant packed big-endian (gcc's rule). */
    chr(t, "'\\1234'\n", (0123 << 8) | '4');
    chr(t, "'ab'\n", ('a' << 8) | 'b');
    chr(t, "'\\\\'\n", '\\');
    chr(t, "'\\''\n", '\'');
    /* Plain char is SIGNED on x86_64-linux, so '\xff' is -1. */
    chr(t, "'\\xff'\n", -1);
    chr(t, "L'x'\n", 'x');

    lex_err(t, "'\\x100'\n", STD_C17); /* out of range for char */
    lex_err(t, "'\\x'\n", STD_C17);    /* no hex digits */
    /* u8'a' in C17: the PP never fuses u8 with a single quote, so this is
     * identifier `u8` followed by a char constant — two tokens, exactly
     * as gcc -std=c17 sees it (the error surfaces later, from the
     * parser: "'u8' undeclared"). The sprint file's "error in c17" is the
     * C23-lexer view; verified against gcc. */
    {
        LexFix f2;
        TokenList tl2 = lex_src(&f2, "u8'a'\n", STD_C17);
        T_ASSERT_EQ_INT(t, f2.errors, 0);
        T_ASSERT(t, tl2.toks[0].kind == TOK_IDENT);
        T_ASSERT_EQ_STR(t, tl2.toks[0].spelling, "u8");
        T_ASSERT(t, tl2.toks[1].kind == TOK_CHAR_CONST);
        lfix_free(&f2);
    }
    lex_err(t, "'\\q'\n", STD_C17); /* unknown escape */
}

/* String payload check: bytes as a hex string, and the encoding. */
static void str_is(TestCtx *t, const char *src, EncPrefix enc,
                   const char *want_bytes, u32 want_n)
{
    LexFix f;
    TokenList tl = lex_src(&f, src, STD_C17);

    if (f.errors != 0) {
        t_fail(t, __FILE__, __LINE__, "%s: unexpected error", src);
    } else if (tl.toks[0].kind != TOK_STRING) {
        t_fail(t, __FILE__, __LINE__, "%s: not a string", src);
    } else if (tl.toks[0].str.enc != enc) {
        t_fail(t, __FILE__, __LINE__, "%s: wrong encoding prefix", src);
    } else if (tl.toks[0].str.nbytes != want_n ||
               memcmp(tl.toks[0].str.bytes, want_bytes, want_n) != 0) {
        t_fail(t, __FILE__, __LINE__, "%s: payload mismatch (%u bytes)", src,
               (unsigned)tl.toks[0].str.nbytes);
    }
    t->assertions++;
    lfix_free(&f);
}

void test_lex_string_concat(TestCtx *t)
{
    str_is(t, "\"hi\"\n", ENC_NONE, "hi", 2);
    str_is(t, "\"hi\" \"there\"\n", ENC_NONE, "hithere", 7);
    /* Plain + prefixed ADOPTS the prefix (6.4.5p5). */
    str_is(t, "\"a\" L\"b\"\n", ENC_WIDE, "a\0\0\0b\0\0\0", 8);
    str_is(t, "L\"a\" \"b\"\n", ENC_WIDE, "a\0\0\0b\0\0\0", 8);
    /* Escapes are decoded PER LITERAL, before concatenation: "\x12" "3"
     * is two chars, never \x123 (the classic). */
    str_is(t, "\"\\x12\" \"3\"\n", ENC_NONE, "\x12\x33", 2);
    str_is(t, "\"\"\n", ENC_NONE, "", 0);
    str_is(t, "u8\"hi\"\n", ENC_U8, "hi", 2);

    /* Two DIFFERENT prefixes: error (gcc 8 rejects; so do we). */
    lex_err(t, "L\"x\" u\"y\"\n", STD_C17);
    lex_err(t, "u8\"x\" L\"y\"\n", STD_C17);
    lex_err(t, "u\"x\" U\"y\"\n", STD_C17);
}

void test_lex_ucn(TestCtx *t)
{
    /* U+00E9 encodes as UTF-8 c3 a9 in the execution charset. */
    str_is(t, "\"\\u00e9\"\n", ENC_NONE, "\xc3\xa9", 2);
    /* UCN constraints (6.4.3p2): below U+00A0 (except $ @ `) and the
     * surrogate range are errors. */
    lex_err(t, "\"\\u0041\"\n", STD_C17);    /* 'A' — below A0 */
    lex_err(t, "\"\\ud800\"\n", STD_C17);    /* surrogate */
    lex_err(t, "\"\\u00e\"\n", STD_C17);     /* too few digits */
    lex_err(t, "\"\\U0000024\"\n", STD_C17); /* 7 digits, not 8 */
    /* $ @ ` are the sanctioned exceptions. */
    str_is(t, "\"\\u0024\"\n", ENC_NONE, "$", 1);
}

void test_lex_float_consts(TestCtx *t)
{
    LexFix f;
    TokenList tl;

    tl = lex_src(&f, "1.5 .5f 1e10 0x1.8p3 1.0L 1.\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tl.toks[0].kind == TOK_FLOAT_CONST);
    T_ASSERT_EQ_INT(t, tl.toks[0].float_type, FTY_DOUBLE);
    /* The token keeps the EXACT spelling — conversion is Sprint 15's. */
    T_ASSERT_EQ_STR(t, tl.toks[0].spelling, "1.5");
    T_ASSERT_EQ_INT(t, tl.toks[1].float_type, FTY_FLOAT);
    T_ASSERT_EQ_STR(t, tl.toks[1].spelling, ".5f");
    T_ASSERT_EQ_INT(t, tl.toks[2].float_type, FTY_DOUBLE);
    T_ASSERT_EQ_STR(t, tl.toks[3].spelling, "0x1.8p3");
    T_ASSERT_EQ_INT(t, tl.toks[4].float_type, FTY_LDOUBLE);
    lfix_free(&f);

    /* The lexer carries the exact SPELLING and computes no value: the
     * host-strtod seam that used to be asserted here (XD-S08-FPHOST) was
     * retired in Sprint 15, and src/util/softfp.c now converts in the
     * TARGET's format. test_softfp.c owns the value assertions. */

    /* _Float128's two suffixes. `1.0q` was pinned as an ERROR here until
     * D5 gave the type a lexer row -- converted rather than deleted, so
     * the boundary it guarded still has a case below. */
    tl = lex_src(&f, "1.0q 1.0Q 1.0f128 1.0F128\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, tl.toks[0].float_type, FTY_FLOAT128);
    T_ASSERT_EQ_INT(t, tl.toks[1].float_type, FTY_FLOAT128);
    T_ASSERT_EQ_INT(t, tl.toks[2].float_type, FTY_FLOAT128);
    T_ASSERT_EQ_INT(t, tl.toks[3].float_type, FTY_FLOAT128);
    lfix_free(&f);

    tl = lex_src_target(&f, "1.0Q 1.0F128\n", STD_C17, CGF_TARGET_ARM64_LINUX);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, tl.toks[0].float_type, FTY_LDOUBLE);
    T_ASSERT_EQ_INT(t, tl.toks[1].float_type, FTY_FLOAT128);
    lfix_free(&f);

    tl = lex_src(&f,
                 "1.0f32 1.0F32 1.0f64 1.0F64 "
                 "1.0f32x 1.0F32x 1.0f64x 1.0F64x\n",
                 STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, tl.toks[0].float_type, FTY_FLOAT32);
    T_ASSERT_EQ_INT(t, tl.toks[1].float_type, FTY_FLOAT32);
    T_ASSERT_EQ_INT(t, tl.toks[2].float_type, FTY_FLOAT64);
    T_ASSERT_EQ_INT(t, tl.toks[3].float_type, FTY_FLOAT64);
    T_ASSERT_EQ_INT(t, tl.toks[4].float_type, FTY_FLOAT32X);
    T_ASSERT_EQ_INT(t, tl.toks[5].float_type, FTY_FLOAT32X);
    T_ASSERT_EQ_INT(t, tl.toks[6].float_type, FTY_FLOAT64X);
    T_ASSERT_EQ_INT(t, tl.toks[7].float_type, FTY_FLOAT64X);
    lfix_free(&f);

    /* Once p/P starts a hex float's exponent, A-F are no longer hex
     * significand digits. In particular, an F<N>[x] suffix starts at F
     * instead of being swallowed into the exponent and left as double. */
    tl = lex_src(&f,
                 "0x1p0f 0x1p0F "
                 "0x1p0f32 0x1p0F32 0x1p0f64 0x1p0F64 "
                 "0x1p0f32x 0x1p0F32x 0x1p0f64x 0x1p0F64x "
                 "0x1p0f128 0x1p0F128\n",
                 STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, tl.toks[0].float_type, FTY_FLOAT);
    T_ASSERT_EQ_INT(t, tl.toks[1].float_type, FTY_FLOAT);
    T_ASSERT_EQ_INT(t, tl.toks[2].float_type, FTY_FLOAT32);
    T_ASSERT_EQ_INT(t, tl.toks[3].float_type, FTY_FLOAT32);
    T_ASSERT_EQ_INT(t, tl.toks[4].float_type, FTY_FLOAT64);
    T_ASSERT_EQ_INT(t, tl.toks[5].float_type, FTY_FLOAT64);
    T_ASSERT_EQ_INT(t, tl.toks[6].float_type, FTY_FLOAT32X);
    T_ASSERT_EQ_INT(t, tl.toks[7].float_type, FTY_FLOAT32X);
    T_ASSERT_EQ_INT(t, tl.toks[8].float_type, FTY_FLOAT64X);
    T_ASSERT_EQ_INT(t, tl.toks[9].float_type, FTY_FLOAT64X);
    T_ASSERT_EQ_INT(t, tl.toks[10].float_type, FTY_FLOAT128);
    T_ASSERT_EQ_INT(t, tl.toks[11].float_type, FTY_FLOAT128);
    T_ASSERT(t, !tl.toks[0].float_ext_suffix);
    T_ASSERT(t, tl.toks[2].float_ext_suffix);
    T_ASSERT(t, tl.toks[11].float_ext_suffix);
    lfix_free(&f);

    /* A hex float REQUIRES its binary exponent. */
    lex_err(t, "0x1.8\n", STD_C17);
    lex_err(t, "1e\n", STD_C17);
    lex_err(t, "1.0df\n", STD_C17);
    /* Still errors: the accepted spellings above are exact, not a blanket
     * "any letters are a suffix" rule. */
    lex_err(t, "1.0qq\n", STD_C17);
    lex_err(t, "1.0fq\n", STD_C17);
    lex_err(t, "1.0f128x\n", STD_C17);
    lex_err(t, "1.0f32X\n", STD_C17);
    lex_err(t, "1.0f64xx\n", STD_C17);
    lex_err(t, "0x1p0F16\n", STD_C17);
    lex_err(t, "0x1p0F32X\n", STD_C17);
    lex_err(t, "0x1p0F128x\n", STD_C17);
}

void test_lex_spans_survive(TestCtx *t)
{
    LexFix f;
    TokenList tl = lex_src(&f, "int\n  x;\n", STD_C17);

    /* Spans come from the pp-token, so line/col are real. */
    T_ASSERT_EQ_INT(t, tl.toks[0].span.line, 1);
    T_ASSERT_EQ_INT(t, tl.toks[0].span.col, 1);
    T_ASSERT_EQ_INT(t, tl.toks[1].span.line, 2);
    T_ASSERT_EQ_INT(t, tl.toks[1].span.col, 3);
    T_ASSERT(t, tl.toks[0].span.file_id != 0);
    lfix_free(&f);
}
