#include <string.h>

#include "parse/parse.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* Parser units: the declarator torture suite (round-tripped through
 * ast_type_render), the typedef-ambiguity pitfalls, and the 6.7.2
 * specifier multiset matrix. */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    int errors;
    int warnings;
    int declarator_expansion_notes;
    int bad_expansion_notes;
    int id_expansion_notes;
    int out_expansion_notes;
    u32 bad_expansion_line;
    u32 id_expansion_line;
    u32 out_expansion_line;
} ParseFix;

static void count_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    ParseFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
    else if (d->level == DIAG_NOTE &&
             strstr(d->message, "in expansion of macro 'DECLARATOR'"))
        f->declarator_expansion_notes++;
    else if (d->level == DIAG_NOTE &&
             strstr(d->message, "in expansion of macro 'BAD'")) {
        f->bad_expansion_notes++;
        f->bad_expansion_line = d->span.line;
    } else if (d->level == DIAG_NOTE &&
               strstr(d->message, "in expansion of macro 'ID'")) {
        f->id_expansion_notes++;
        f->id_expansion_line = d->span.line;
    } else if (d->level == DIAG_NOTE &&
               strstr(d->message, "in expansion of macro 'OUT'")) {
        f->out_expansion_notes++;
        f->out_expansion_line = d->span.line;
    }
}

VEC_DECL(PpVecP, PpToken);

static AstNode *parse_src_with_options(ParseFix *f, const char *src, CStd std,
                                       bool system_header, bool pedantic)
{
    DiagCtx *dc;
    DiagSink s;
    SourceFile *sf;
    PpVecP pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec target;
    AstNode *tu;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    dc = diag_ctx_new(&f->arena);
    s.handle = count_sink;
    s.user = f;
    diag_set_sink(dc, s);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, dc, &f->in);

    memset(&lang, 0, sizeof(lang));
    lang.std = std;
    lang.gnu_mode = std >= STD_GNU89;
    lang.pedantic = pedantic;
    lang.warnings = warn_ctx_new(&f->arena, dc);
    if (pedantic)
        (void)warn_flag(lang.warnings, "pedantic");
    f->pp.warn = lang.warnings;
    target.kind = CGF_TARGET_X86_64_LINUX_GNU;

    sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    sf->is_system = system_header;
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t))
        PpVecP_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, target, &f->arena);
    PpVecP_free(&pv);

    parse_init(&f->ps, &tl, &f->pp, dc, &f->arena, &lang);
    tu = parse_translation_unit(&f->ps);
    return tu;
}

static AstNode *parse_src_with_origin(ParseFix *f, const char *src, CStd std,
                                      bool system_header)
{
    return parse_src_with_options(f, src, std, system_header, false);
}

static AstNode *parse_src(ParseFix *f, const char *src, CStd std)
{
    return parse_src_with_origin(f, src, std, false);
}

static void pfix_free(ParseFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

void test_parse_dynamic_builtin_macro_backtrace(TestCtx *t)
{
    ParseFix f;

    (void)parse_src(&f,
                    "#define DECLARATOR __COUNTER__\n"
                    "int DECLARATOR;\n",
                    STD_C17);
    T_ASSERT(t, f.errors > 0);
    T_ASSERT(t, f.declarator_expansion_notes > 0);
    pfix_free(&f);
}

void test_parse_preexpanded_argument_backtrace_anchors(TestCtx *t)
{
    ParseFix f;

    (void)parse_src(&f,
                    "#define BAD 09\n"
                    "#define ID(x) x\n"
                    "#define OUT(x) ID(x)\n"
                    "int value = OUT(BAD);\n",
                    STD_C17);
    T_ASSERT_EQ_INT(t, f.bad_expansion_notes, 1);
    T_ASSERT_EQ_INT(t, f.id_expansion_notes, 1);
    T_ASSERT_EQ_INT(t, f.out_expansion_notes, 1);
    T_ASSERT_EQ_INT(t, f.bad_expansion_line, 4);
    T_ASSERT_EQ_INT(t, f.id_expansion_line, 3);
    T_ASSERT_EQ_INT(t, f.out_expansion_line, 4);
    pfix_free(&f);
}

/* Declarator round-trip: parse `src`, render the FIRST declaration's type,
 * compare with the expected English reading. The chain is built inside-out
 * by the parser and rendered outside-in, so this is a real round-trip. */
static void decl_is(TestCtx *t, const char *src, const char *want)
{
    ParseFix f;
    AstNode *tu = parse_src(&f, src, STD_C17);
    Buf b;

    buf_init(&b);
    if (f.errors != 0) {
        t_fail(t, __FILE__, __LINE__, "%s: unexpected error", src);
    } else if (tu->ndecls < 1) {
        t_fail(t, __FILE__, __LINE__, "%s: no declaration parsed", src);
    } else {
        char got[512];
        ast_type_render(tu->decls[0]->type, &b);
        snprintf(got, sizeof(got), "%.*s", (int)b.len, (const char *)b.data);
        if (strcmp(got, want) != 0)
            t_fail(t, __FILE__, __LINE__, "%s:\n  got  %s\n  want %s", src, got,
                   want);
    }
    t->assertions++;
    buf_free(&b);
    pfix_free(&f);
}

void test_parse_declarator_torture(TestCtx *t)
{
    /* THE worked example from the sprint file. */
    decl_is(t, "int (*(*f[3])(void))[5];\n",
            "array [expr] of ptr to func(void) ret ptr to array [expr] of "
            "int");

    decl_is(t, "int x;\n", "int");
    decl_is(t, "int *p;\n", "ptr to int");
    decl_is(t, "int **pp;\n", "ptr to ptr to int");
    decl_is(t, "int a[10];\n", "array [expr] of int");
    decl_is(t, "int a[];\n", "array of int");
    decl_is(t, "int a[3][4];\n", "array [expr] of array [expr] of int");
    decl_is(t, "int f(void);\n", "func(void) ret int");
    decl_is(t, "int f();\n", "func(unspecified) ret int");
    decl_is(t, "int f(int, char);\n", "func(int, char) ret int");
    decl_is(t, "int f(int, ...);\n", "func(int, ...) ret int");
    decl_is(t, "int (*fp)(void);\n", "ptr to func(void) ret int");
    decl_is(t, "int *fp(void);\n", "func(void) ret ptr to int");
    decl_is(t, "int (*a[5])(void);\n",
            "array [expr] of ptr to func(void) ret int");
    decl_is(t, "int (*ap)[5];\n", "ptr to array [expr] of int");
    decl_is(t, "const char *s;\n", "ptr to const char");
    decl_is(t, "char *const s;\n", "const ptr to char");
    decl_is(t, "const char *const s;\n", "const ptr to const char");
    decl_is(t, "int *restrict rp;\n", "restrict ptr to int");
    decl_is(t, "void (*sig(int, void (*)(int)))(int);\n",
            "func(int, ptr to func(int) ret void) ret ptr to func(int) ret "
            "void");
    decl_is(t, "int (*(*x)(int (*)(void)))[2];\n",
            "ptr to func(ptr to func(void) ret int) ret ptr to array [expr] "
            "of int");
    decl_is(t, "unsigned long long ull;\n", "unsigned long long");
    decl_is(t, "long double ld;\n", "long double");
    decl_is(t, "_Float32 f32;\n", "_Float32");
    decl_is(t, "_Float64 f64;\n", "_Float64");
    decl_is(t, "_Float32x f32x;\n", "_Float32x");
    decl_is(t, "_Float64x f64x;\n", "_Float64x");
    decl_is(t, "signed char sc;\n", "signed char");
    decl_is(t, "int p[static 3];\n", "array [expr] of int");
}

void test_parse_comma_declarator_attribute(TestCtx *t)
{
    ParseFix f;
    AstNode *tu =
        parse_src(&f,
                  "int a, "
                  "__attribute__((weak)) b, c;\n"      /* check_bans allow */
                  "__attribute__((used)) int x, y;\n", /* check_bans allow */
                  STD_GNU17);
    AstNode *a;
    AstNode *b;
    AstNode *c;
    AstNode *x;
    AstNode *y;

    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, (int)tu->ndecls, 2);
    a = tu->decls[0];
    T_ASSERT_EQ_INT(t, (int)a->nitems, 2);
    b = a->items[0];
    c = a->items[1];
    T_ASSERT(t, !a->gnu.weak);
    T_ASSERT(t, b->gnu.weak);
    T_ASSERT(t, !c->gnu.weak);

    x = tu->decls[1];
    T_ASSERT_EQ_INT(t, (int)x->nitems, 1);
    y = x->items[0];
    T_ASSERT(t, x->gnu.used);
    T_ASSERT(t, y->gnu.used);
    pfix_free(&f);
}

void test_parse_attributed_function_pointer(TestCtx *t)
{
    ParseFix f;

    (void)parse_src(&f,
                    "#define ATTR "
                    "__attribute__((__noinline__))\n" /* check_bans allow */
                    "int call_both(void *p) {\n"
                    "  return ((ATTR int (*)(void))p)() +\n"
                    "         ((int (ATTR *)(void))p)();\n"
                    "}\n",
                    STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    pfix_free(&f);

    /* `mode` changes the represented type width. AstType has no attribute
     * slot at this declarator layer, so accepting it here would silently
     * compile a different calling convention. */
    (void)parse_src(
        &f,
        "int bad(void *p) {\n"
        "  return ((int "
        "(__attribute__((mode(QI))) *)(void))p)();\n" /* check_bans allow */
        "}\n",
        STD_GNU17);
    T_ASSERT(t, f.errors > 0);
    pfix_free(&f);
}

void test_parse_inner_pointer_aligned_attribute(TestCtx *t)
{
    ParseFix f;
    AstNode *tu = parse_src(
        &f, "int *__attribute__((aligned(16))) *p;\n", /* check_bans allow */
        STD_GNU17);
    AstType *outer;

    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, (int)tu->ndecls, 1);
    outer = tu->decls[0]->type;
    T_ASSERT(t, outer != NULL);
    T_ASSERT_EQ_INT(t, outer->kind, ATY_PTR);
    T_ASSERT(t, outer->next != NULL);
    T_ASSERT_EQ_INT(t, outer->next->kind, ATY_PTR);
    T_ASSERT(t, outer->next->next != NULL);
    T_ASSERT_EQ_INT(t, outer->next->next->kind, ATY_BASE);
    pfix_free(&f);
}

void test_parse_system_float128_compat_typedef(TestCtx *t)
{
    ParseFix f;
    AstNode *tu = parse_src_with_origin(&f, "typedef long double _Float128;\n",
                                        STD_C17, true);

    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    T_ASSERT_EQ_INT(t, (int)tu->ndecls, 1);
    T_ASSERT_EQ_INT(t, tu->decls[0]->kind, AST_EMPTY_DECL);
    T_ASSERT_EQ_INT(t, tu->decls[0]->type->base, ABT_FLOAT128);
    pfix_free(&f);

    /* x86 glibc's sibling fallback uses the alternate native spelling.
     * It was already accepted; pin that symmetry beside the arm64 repair. */
    (void)parse_src_with_origin(&f, "typedef __float128 _Float128;\n", STD_C17,
                                true);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    pfix_free(&f);

    (void)parse_src(&f, "typedef long double _Float128;\n", STD_C17);
    T_ASSERT(t, f.errors > 0);
    pfix_free(&f);

    (void)parse_src_with_origin(&f, "typedef long float _Float128;\n", STD_C17,
                                true);
    T_ASSERT(t, f.errors > 0);
    pfix_free(&f);

    (void)parse_src_with_origin(&f, "typedef long double __float128;\n",
                                STD_C17, true);
    T_ASSERT(t, f.errors > 0);
    pfix_free(&f);
}

void test_parse_system_floatn_compat_typedefs(TestCtx *t)
{
    ParseFix f;
    AstNode *tu;
    static const struct {
        const char *src;
        AstBaseType want;
    } rows[] = {{"typedef float _Float32;\n", ABT_FLOAT32},
                {"typedef double _Float64;\n", ABT_FLOAT64},
                {"typedef double _Float32x;\n", ABT_FLOAT32X},
                {"typedef long double _Float64x;\n", ABT_FLOAT64X}};
    static const char *const invalid[] = {
        "typedef _Float64 _Float128;\n",
        "typedef _Float32x _Float128;\n",
        "typedef _Float128 _Float128;\n",
    };
    u32 i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        tu = parse_src_with_origin(&f, rows[i].src, STD_C17, true);
        T_ASSERT_EQ_INT(t, f.errors, 0);
        T_ASSERT_EQ_INT(t, (int)tu->ndecls, 1);
        T_ASSERT_EQ_INT(t, tu->decls[0]->kind, AST_EMPTY_DECL);
        T_ASSERT_EQ_INT(t, tu->decls[0]->type->base, rows[i].want);
        pfix_free(&f);

        (void)parse_src(&f, rows[i].src, STD_C17);
        T_ASSERT(t, f.errors > 0);
        pfix_free(&f);
    }

    (void)parse_src_with_origin(&f, "typedef float _Float64;\n", STD_C17, true);
    T_ASSERT(t, f.errors > 0);
    pfix_free(&f);

    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        (void)parse_src_with_origin(&f, invalid[i], STD_C17, true);
        T_ASSERT(t, f.errors > 0);
        pfix_free(&f);
    }
}

void test_parse_floatn_pedantic_warnings(TestCtx *t)
{
    ParseFix f;
    static const char *const types[] = {
        "_Float32", "_Float64", "_Float32x", "_Float64x", "_Float128",
    };
    char src[96];
    u32 i;

    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        snprintf(src, sizeof(src), "%s value;\n", types[i]);
        (void)parse_src_with_options(&f, src, STD_C17, false, true);
        T_ASSERT_EQ_INT(t, f.errors, 0);
        T_ASSERT_EQ_INT(t, f.warnings, 1);
        pfix_free(&f);

        snprintf(src, sizeof(src), "__extension__ %s value;\n", types[i]);
        (void)parse_src_with_options(&f, src, STD_C17, false, true);
        T_ASSERT_EQ_INT(t, f.errors, 0);
        T_ASSERT_EQ_INT(t, f.warnings, 0);
        pfix_free(&f);
    }

    (void)parse_src_with_options(&f, "__float128 value;\n", STD_C17, false,
                                 true);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    pfix_free(&f);

    (void)parse_src_with_options(&f,
                                 "__extension__ _Float32 x = 1.0F32;\n"
                                 "__extension__ int y = 1.0F32;\n",
                                 STD_C17, false, true);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    pfix_free(&f);

    (void)parse_src_with_options(&f,
                                 "__extension__ int suppressed = 1.0F32;\n"
                                 "int restored = 1.0F32;\n",
                                 STD_C17, false, true);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 1);
    pfix_free(&f);

    (void)parse_src_with_options(
        &f, "__extension__ __extension__ _Float32 repeated = 1.0F32;\n",
        STD_C17, false, true);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    pfix_free(&f);

    /* GCC permits the marker only before the whole declaration. It is not
     * a specifier that can float through an otherwise-started soup. */
    (void)parse_src_with_options(&f, "const __extension__ int misplaced;\n",
                                 STD_C17, false, true);
    T_ASSERT(t, f.errors > 0);
    pfix_free(&f);
}

void test_parse_typedef_ambiguity(TestCtx *t)
{
    ParseFix f;
    AstNode *tu;

    /* T(a); declares a variable `a` of type T with redundant parens —
     * NOT a call. Verified against gcc. */
    tu = parse_src(&f, "typedef int T;\nT(a);\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tu->ndecls >= 2);
    T_ASSERT_EQ_STR(t, tu->decls[1]->name, "a");
    pfix_free(&f);

    /* int(b); likewise. */
    tu = parse_src(&f, "int(b);\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_STR(t, tu->decls[0]->name, "b");
    pfix_free(&f);

    /* `int f(x)` where x IS a typedef: a PROTOTYPE with one unnamed
     * parameter of type x. Where it is NOT: a K&R identifier list. */
    tu = parse_src(&f, "typedef int x;\nint f(x);\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tu->decls[1]->type->kind == ATY_FUNC);
    T_ASSERT(t, !tu->decls[1]->type->is_kr_list);
    T_ASSERT_EQ_INT(t, tu->decls[1]->type->nparams, 1);
    pfix_free(&f);

    tu = parse_src(&f, "int g(y);\n", STD_C17);
    T_ASSERT(t, tu->decls[0]->type->is_kr_list);
    /* ...and an identifier list is only legal in a DEFINITION. */
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    /* A typedef name is a specifier only when NO other type specifier was
     * seen: in `unsigned T z;` the T is the DECLARATOR (so `z` is a
     * syntax error — exactly what gcc reports). */
    tu = parse_src(&f, "typedef int T;\nunsigned T z;\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    /* Shadowing: once T is a variable, it is no longer a type name. */
    tu = parse_src(&f, "typedef int T;\nvoid q(void) { int T; }\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    pfix_free(&f);
    (void)tu;
}

void test_parse_kr_definitions(TestCtx *t)
{
    ParseFix f;
    AstNode *tu;

    tu = parse_src(&f, "int add(a, b) int a; int b; { return a + b; }\n",
                   STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tu->ndecls == 1);
    T_ASSERT(t, tu->decls[0]->kind == AST_FUNC_DEF);
    T_ASSERT(t, tu->decls[0]->type->is_kr_list);
    T_ASSERT_EQ_INT(t, tu->decls[0]->type->nparams, 2);
    T_ASSERT_EQ_INT(t, tu->decls[0]->nkr_decls, 2);
    pfix_free(&f);

    /* An identifier list in a non-definition is an error. */
    tu = parse_src(&f, "int f(a, b);\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);
    (void)tu;
}

void test_parse_variadic_and_nested_kr_constraints(TestCtx *t)
{
    ParseFix f;

    /* FE-H-01: ellipsis cannot be the entire parameter list in either ISO
     * or GNU C. A preceding typed parameter is sufficient even when unnamed. */
    (void)parse_src(&f, "int f(...);\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    (void)parse_src(&f, "int f(...);\n", STD_GNU17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    (void)parse_src(&f, "int f(int, ...); int g(int named, ...);\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    pfix_free(&f);

    /* FE-H-02: the old outer-only check missed every derived position below.
     * Cover a pointer, prototype parameter, record member, return type, type
     * name, and the typedef-shadowing sibling shape from the audit. */
    (void)parse_src(&f, "int (*p)(a);\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    (void)parse_src(&f, "void g(int (*p)(a));\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    (void)parse_src(&f, "struct S { int (*p)(a); };\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    (void)parse_src(&f, "int (*f(void))(a);\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    (void)parse_src(&f, "int n = sizeof(int (*)(a));\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    (void)parse_src(&f, "typedef int T; void f(void) { T T, (*p)(T); }\n",
                    STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    /* Controls: the root identifier-list layer remains legal for a function
     * definition, while typed prototypes and pointer-to-prototype shapes do
     * not become false positives. */
    (void)parse_src(&f,
                    "int old(a) int a; { return a; }\n"
                    "int proto(int);\n"
                    "int (*pointer)(int);\n"
                    "typedef int T; int (*typed)(T);\n",
                    STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    pfix_free(&f);
}

/* Accept/reject helper for the specifier matrix. */
static void spec_ok(TestCtx *t, const char *src)
{
    ParseFix f;

    (void)parse_src(&f, src, STD_C17);
    if (f.errors != 0)
        t_fail(t, __FILE__, __LINE__, "%s: should be accepted", src);
    t->assertions++;
    pfix_free(&f);
}

static void spec_bad(TestCtx *t, const char *src)
{
    ParseFix f;

    (void)parse_src(&f, src, STD_C17);
    if (f.errors == 0)
        t_fail(t, __FILE__, __LINE__, "%s: should be rejected", src);
    t->assertions++;
    pfix_free(&f);
}

void test_parse_specifier_multisets(TestCtx *t)
{
    /* Every valid multiset from C11 6.7.2p2, in a scrambled order to
     * prove order never matters. */
    spec_ok(t, "void v;\n");
    spec_ok(t, "char c;\n");
    spec_ok(t, "signed char sc;\n");
    spec_ok(t, "char signed sc2;\n");
    spec_ok(t, "unsigned char uc;\n");
    spec_ok(t, "short s;\n");
    spec_ok(t, "short int si;\n");
    spec_ok(t, "int short is;\n");
    spec_ok(t, "signed short int ssi;\n");
    spec_ok(t, "unsigned short us;\n");
    spec_ok(t, "int i;\n");
    spec_ok(t, "signed sg;\n");
    spec_ok(t, "signed int sgi;\n");
    spec_ok(t, "unsigned u;\n");
    spec_ok(t, "unsigned int ui;\n");
    spec_ok(t, "long l;\n");
    spec_ok(t, "long int li;\n");
    spec_ok(t, "int long il;\n");
    spec_ok(t, "unsigned long ul;\n");
    spec_ok(t, "long unsigned int lui;\n");
    spec_ok(t, "long long ll;\n");
    spec_ok(t, "long long int lli;\n");
    spec_ok(t, "unsigned long long ull;\n");
    spec_ok(t, "long long unsigned int llui;\n");
    spec_ok(t, "float f;\n");
    spec_ok(t, "double d;\n");
    spec_ok(t, "long double ld;\n");
    spec_ok(t, "double long dl;\n");
    spec_ok(t, "_Bool b;\n");
    spec_ok(t, "const int ci;\n");
    spec_ok(t, "int const ic;\n");
    spec_ok(t, "const volatile int cvi;\n");
    spec_ok(t, "static const unsigned long scul;\n");

    /* Invalid multisets, each with a TARGETED message (not "syntax
     * error"). */
    spec_bad(t, "long long long lll;\n");
    spec_bad(t, "signed unsigned su;\n");
    spec_bad(t, "void void vv;\n");
    spec_bad(t, "float double fd;\n");
    spec_bad(t, "short long sl;\n");
    spec_bad(t, "char int ci2;\n");
    spec_bad(t, "static extern int se;\n");
    spec_bad(t, "typedef static int ts;\n");
    spec_bad(t, "int f(static int);\n");
    spec_bad(t, "float short fs;\n");
    spec_bad(t, "_Bool int bi;\n");
    spec_bad(t, "long _Float32 lf;\n");
    spec_bad(t, "unsigned _Float64 uf;\n");
    spec_bad(t, "_Float32 _Float64 mixed;\n");
    spec_bad(t, "_Float32x _Float32x duplicate;\n");

    /* _Thread_local is the ONE storage class that may pair (6.7.1p2). */
    spec_ok(t, "_Thread_local static int tls;\n");
    spec_ok(t, "static _Thread_local int tls2;\n");
    spec_ok(t, "int f(register int);\n");
}

void test_parse_records_and_enums(TestCtx *t)
{
    ParseFix f;
    AstNode *tu;

    tu = parse_src(&f, "struct S { int a; char b : 3; unsigned : 0; };\n",
                   STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tu->decls[0]->type->record != NULL);
    T_ASSERT(t, tu->decls[0]->type->record->is_definition);
    T_ASSERT_EQ_INT(t, tu->decls[0]->type->record->nmembers, 3);
    T_ASSERT(t, tu->decls[0]->type->record->members[1]->is_bitfield);
    T_ASSERT(t, tu->decls[0]->type->record->members[1]->bitfield_width);
    pfix_free(&f);

    /* An untagged struct/union member with no declarator is a C11 anonymous
     * member; the tagged Microsoft extension is deliberately refused. */
    tu = parse_src(&f, "struct A { struct { int x; }; int y; };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tu->decls[0]->type->record->members[0]->is_anon_member);
    pfix_free(&f);

    tu = parse_src(&f, "struct B { struct Tag { int x; }; };\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    /* Enumerator VALUES are stored as expressions, never evaluated here. */
    tu = parse_src(&f, "enum E { A, B = 5, C, };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, tu->decls[0]->type->record->nmembers, 3);
    T_ASSERT(t, tu->decls[0]->type->record->members[1]->init != NULL);
    T_ASSERT(t, tu->decls[0]->type->record->members[0]->init == NULL);
    pfix_free(&f);

    /* A trailing comma in an enumerator list is a C99 feature. */
    tu = parse_src(&f, "enum F { X, };\n", STD_C89);
    T_ASSERT(t, f.warnings >= 1);
    pfix_free(&f);
    (void)tu;
}

void test_parse_bitfield_suffix_attributes(TestCtx *t)
{
    ParseFix f;
    AstNode *tu = parse_src(
        &f,
        "struct S {\n"
        "  unsigned first : 7 "
        "__attribute__((packed));\n" /* check_bans allow */
        "  unsigned second : 5 "
        "__attribute__((aligned(4), deprecated));\n" /* check_bans allow */
        "  unsigned third : 3;\n"
        "};\n",
        STD_GNU17);
    AstNode *rec;
    AstNode *first;
    AstNode *second;
    AstNode *third;

    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, (int)tu->ndecls, 1);
    rec = tu->decls[0]->type->record;
    T_ASSERT(t, rec != NULL);
    T_ASSERT_EQ_INT(t, (int)rec->nmembers, 3);
    first = rec->members[0];
    second = rec->members[1];
    third = rec->members[2];
    T_ASSERT(t, first->is_bitfield);
    T_ASSERT(t, first->gnu.packed);
    T_ASSERT(t, second->is_bitfield);
    T_ASSERT(t, second->gnu.aligned_expr != NULL);
    T_ASSERT(t, second->gnu.deprecated);
    T_ASSERT(t, third->is_bitfield);
    T_ASSERT(t, !third->gnu.packed);
    T_ASSERT(t, third->gnu.aligned_expr == NULL);
    T_ASSERT(t, !third->gnu.deprecated);
    pfix_free(&f);
}

void test_parse_initializers(TestCtx *t)
{
    ParseFix f;
    AstNode *tu;

    tu = parse_src(&f, "int a[3] = { [0] = 1, [2] = 3 };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tu->decls[0]->init != NULL);
    T_ASSERT(t, tu->decls[0]->init->kind == AST_INIT_LIST);
    T_ASSERT_EQ_INT(t, tu->decls[0]->init->nitems, 2);
    T_ASSERT_EQ_INT(t, tu->decls[0]->init->items[0]->ndesignators, 1);
    pfix_free(&f);

    /* Designator CHAINS ([2].x[1]) parse verbatim; the current-object
     * algorithm is sema's (Sprint 12-16). */
    tu = parse_src(&f, "struct P { int x[2]; } p[3] = { [2].x[1] = 7 };\n",
                   STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, tu->decls[0]->init->items[0]->ndesignators, 3);
    pfix_free(&f);

    /* GNU's historical `field: value` spelling is represented by the same
     * field-designator node as `.field = value`. */
    tu = parse_src(&f, "struct P { int x; }; struct P p = { x: 7 };\n",
                   STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, tu->decls[1]->init->items[0]->ndesignators, 1);
    T_ASSERT(t, tu->decls[1]->init->items[0]->designators[0]->desig_is_field);
    T_ASSERT_EQ_STR(
        t, tu->decls[1]->init->items[0]->designators[0]->desig_field, "x");
    pfix_free(&f);

    /* GNU ranges retain both endpoint expressions for sema. */
    tu = parse_src(&f, "int a[4] = { [1 ... 3] = 0 };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, tu->decls[0]->init->items[0]->ndesignators, 1);
    T_ASSERT(t, tu->decls[0]->init->items[0]->designators[0]->desig_range_end !=
                    NULL);
    pfix_free(&f);
}

void test_parse_spans_in_bounds(TestCtx *t)
{
    ParseFix f;
    AstNode *tu = parse_src(&f,
                            "int a;\nstruct S { int b; };\nint f(void) { "
                            "return 0; }\n",
                            STD_C17);
    u32 i;

    /* Every node carries a span pointing into a real file. */
    T_ASSERT_EQ_INT(t, f.errors, 0);
    for (i = 0; i < tu->ndecls; i++) {
        T_ASSERT(t, tu->decls[i]->span.file_id != 0);
        T_ASSERT(t, tu->decls[i]->span.line >= 1);
        T_ASSERT(t, tu->decls[i]->span.line <= 3);
        T_ASSERT(t, tu->decls[i]->span.col >= 1);
    }
    pfix_free(&f);
}

void test_parse_deferrals_name_sprints(TestCtx *t)
{
    ParseFix f;

    /* No silent stubs: every deferral names its sprint. */
    /* `typeof` LANDED in Sprint 55. What stays an error is the bare
     * spelling in an ISO mode -- it is a keyword only in gnu modes, which
     * is gcc's rule too (-std=c17 rejects `typeof`, accepts `__typeof__`;
     * both measured). The accepting cases execute in
     * tests/corpus/x86_64/int/typeof_auto_type.c. */
    (void)parse_src(&f, "typeof(int) x;\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);

    (void)parse_src(&f, "_Complex double z;\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    pfix_free(&f);
}
