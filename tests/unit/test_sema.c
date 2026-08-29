#include <string.h>

#include "parse/parse.h"
#include "sema/sema.h"
#include "unit.h"
#include "util/arena.h"
#include "util/dlev.h"
#include "warn/warn.h"

/* Sema units: the compatibility/composite truth tables, basic-type
 * interning, the four namespaces, and the full 6.2.2 linkage matrix. */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    Sema sema;
    DiagCtx *dc;
    int errors;
    int warnings;
} SemaFix;

static void sema_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    SemaFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
}

VEC_DECL(PpVecS, PpToken);

/* Runs the whole front end over `src` and leaves the Sema state open for
 * inspection — the file scope is still current, so scope_lookup works. */
static void run_sema_opts(SemaFix *f, const char *src, CStd std, bool pedantic)
{
    DiagSink sink;
    SourceFile *sf;
    PpVecS pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec target;
    AstNode *tu;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = sema_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, f->dc, &f->in);

    memset(&lang, 0, sizeof(lang));
    lang.std = std;
    lang.gnu_mode = std >= STD_GNU89;
    lang.warnings = warn_ctx_new(&f->arena, f->dc);
    lang.pedantic = pedantic;
    if (pedantic)
        (void)warn_flag(lang.warnings, "pedantic");
    f->pp.warn = lang.warnings;
    target.kind = CGF_TARGET_X86_64_LINUX_GNU;

    sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t))
        PpVecS_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, target, &f->arena);
    PpVecS_free(&pv);

    parse_init(&f->ps, &tl, &f->pp, f->dc, &f->arena, &lang);
    tu = parse_translation_unit(&f->ps);
    sema_init(&f->sema, &f->arena, f->dc, &f->in, &lang, target);
    sema_run(&f->sema, tu);
}

static void run_sema(SemaFix *f, const char *src, CStd std)
{
    run_sema_opts(f, src, std, false);
}

static void sfix_free(SemaFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

/* --- basic types are interned -------------------------------------------- */

void test_sema_basic_types_interned(TestCtx *t)
{
    /* One canonical node per basic kind, so basics compare by POINTER —
     * which is what makes type_compatible's fast path correct. */
    T_ASSERT(t, type_basic(TY_INT) == type_basic(TY_INT));
    T_ASSERT(t, type_basic(TY_CHAR) == type_basic(TY_CHAR));
    T_ASSERT(t, type_basic(TY_INT) != type_basic(TY_LONG));
    /* 6.2.5p15: char, signed char and unsigned char are THREE distinct
     * types, always — even on a target where char is signed. */
    T_ASSERT(t, type_basic(TY_CHAR) != type_basic(TY_SCHAR));
    T_ASSERT(t, type_basic(TY_CHAR) != type_basic(TY_UCHAR));
    T_ASSERT(t, type_basic(TY_SCHAR) != type_basic(TY_UCHAR));
    T_ASSERT(t, !type_compatible(type_basic(TY_CHAR), type_basic(TY_SCHAR)));
}

void test_sema_rejects_invalid_lowering_inputs(TestCtx *t)
{
    SemaFix f;

    run_sema(&f, "int f(const void) { return 0; }\n", STD_C17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);

    run_sema(&f,
             "int f(int n, ...) { __builtin_va_list ap; "
             "return __builtin_va_arg(sizeof ap, int); }\n",
             STD_C17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);

    run_sema(&f, "int f(double d) { return d == f; }\n", STD_C17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);

    run_sema(&f, "int f(double a, double b) { a %= b; return 0; }\n", STD_C17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);

    run_sema(&f, "int f(int *p, double d) { p += d; return 0; }\n", STD_C17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);

    run_sema(&f, "int f(int *p, int *q) { p *= q; return 0; }\n", STD_C17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);
}

void test_sema_gnu_extern_void_symbol(TestCtx *t)
{
    SemaFix f;
    const char *marker_name;
    Symbol *marker;

    run_sema(&f,
             "extern void marker;\n"
             "static unsigned long address = (unsigned long)&marker;\n",
             STD_GNU17);
    marker_name = intern_str(&f.in, intern_cstr(&f.in, "marker"));
    marker = scope_lookup(f.sema.file_scope, marker_name, NS_ORDINARY);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, marker != NULL);
    T_ASSERT(t, marker && marker->type && marker->type->kind == TY_VOID);
    T_ASSERT(t, marker && marker->linkage == LINK_EXTERNAL);
    T_ASSERT(t, marker && !marker->defined && !marker->tentative);
    sfix_free(&f);

    /* The extension is declaration-only: an ordinary void object remains a
     * constraint violation rather than becoming a zero-sized definition. */
    run_sema(&f, "void marker;\n", STD_GNU17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);
}

void test_sema_gnu_alloca_alias(TestCtx *t)
{
    SemaFix f;
    const char *alloca_name;
    Symbol *alloca_sym;

    run_sema(&f, "void f(unsigned long n) { void *p = alloca(n); (void)p; }\n",
             STD_GNU89);
    alloca_name = intern_str(&f.in, intern_cstr(&f.in, "alloca"));
    alloca_sym = scope_lookup(f.sema.file_scope, alloca_name, NS_ORDINARY);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    /* A hosted GNU call is the plain spelling of __builtin_alloca, not an
     * implicitly declared external function that will fail at link time. */
    T_ASSERT(t, alloca_sym == NULL);
    sfix_free(&f);

    run_sema(&f,
             "void f(unsigned long n) { void *p = (void *)alloca(n); "
             "(void)p; }\n",
             STD_C89);
    alloca_name = intern_str(&f.in, intern_cstr(&f.in, "alloca"));
    alloca_sym = scope_lookup(f.sema.file_scope, alloca_name, NS_ORDINARY);
    T_ASSERT(t, alloca_sym && alloca_sym->kind == SYM_FUNC);
    sfix_free(&f);
}

void test_sema_gnu_compound_literal_array_initializer(TestCtx *t)
{
    SemaFix f;
    const char *array_name;
    Symbol *array;

    run_sema(&f,
             "static const unsigned short array[] = "
             "(const unsigned short[]){ 1, 2, 3 };\n",
             STD_GNU17);
    array_name = intern_str(&f.in, intern_cstr(&f.in, "array"));
    array = scope_lookup(f.sema.file_scope, array_name, NS_ORDINARY);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, array != NULL);
    T_ASSERT(t, array && array->type && array->type->kind == TY_ARRAY);
    T_ASSERT(t, array && array->type && array->type->has_size);
    T_ASSERT(t, array && array->type && array->type->size == 3);
    sfix_free(&f);

    /* This is a value copy, so source/destination element qualifiers do not
     * make otherwise identical array types incompatible. */
    run_sema(&f,
             "static const int qualified[] = (int[]){ 1, 2 }; "
             "static int unqualified[] = (const int[]){ 3, 4 };\n",
             STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    /* The extension is a constant-image rule, not general array assignment:
     * an automatic destination and an ordinary array expression still fail. */
    run_sema(&f,
             "void f(void) { int array[] = (int[]){ 1, 2 }; (void)array; }\n",
             STD_GNU17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);

    run_sema(&f, "static int source[2]; static int array[2] = source;\n",
             STD_GNU17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);

    /* Both completed bounds and element types remain compatibility gates. */
    run_sema(&f, "static int array[1] = (int[2]){ 1, 2 };\n", STD_GNU17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);

    run_sema(&f, "static int array[] = (long[]){ 1, 2 };\n", STD_GNU17);
    T_ASSERT(t, f.errors > 0);
    sfix_free(&f);

    run_sema_opts(&f, "static int array[] = (int[]){ 1, 2 };\n", STD_C17, true);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 1);
    sfix_free(&f);

    /* Both declaration- and expression-position `__extension__` cover a
     * warning that sema emits after the parser's suppression scope ends. */
    run_sema_opts(&f, "__extension__ static int array[] = (int[]){ 1, 2 };\n",
                  STD_C17, true);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    sfix_free(&f);

    run_sema_opts(&f, "static int array[] = __extension__ (int[]){ 1, 2 };\n",
                  STD_C17, true);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    sfix_free(&f);
}

/* --- the compatibility truth table --------------------------------------- */

typedef struct {
    Arena ar;
} TypeFix;

static Type *P(TypeFix *tf, Type *to)
{
    return type_ptr(&tf->ar, to);
}

static Type *Q(TypeFix *tf, Type *of, unsigned q)
{
    return type_qualify(&tf->ar, of, q);
}

static Type *A(TypeFix *tf, Type *elem, bool sized, u64 n)
{
    Type *a = type_array(&tf->ar, elem);

    a->has_size = sized;
    a->size = n;
    return a;
}

void test_sema_runtime_sized_array_predicate(TestCtx *t)
{
    TypeFix tf;
    Type *inner;
    Type *outer;
    Type *ptr;

    arena_init(&tf.ar);
    inner = A(&tf, type_basic(TY_INT), false, 0);
    inner->is_vla = true;
    outer = A(&tf, inner, true, 3);
    ptr = P(&tf, inner);

    T_ASSERT(t, type_is_runtime_sized_array(inner));
    T_ASSERT(t, type_is_runtime_sized_array(outer));
    T_ASSERT(t, !type_is_runtime_sized_array(ptr));
    T_ASSERT(t, !type_is_runtime_sized_array(type_basic(TY_INT)));
    arena_free_all(&tf.ar);
}

static Type *F(TypeFix *tf, Type *ret, Type **params, u32 n, bool proto,
               bool variadic)
{
    Type *f = type_func(&tf->ar, ret);

    f->has_proto = proto;
    f->nparams = n;
    f->params = params;
    f->variadic = variadic;
    return f;
}

static Type *KRF(TypeFix *tf, Type *ret, Type **params, u32 n)
{
    Type *f = F(tf, ret, NULL, 0, false, false);

    f->old_style_definition = true;
    f->kr_definition = true;
    f->old_style_params = params;
    f->nold_style_params = n;
    return f;
}

static Type *ODF(TypeFix *tf, Type *ret)
{
    Type *f = F(tf, ret, NULL, 0, false, false);

    f->old_style_definition = true;
    return f;
}

static void compat_is(TestCtx *t, const char *what, Type *a, Type *b, bool want)
{
    bool got = type_compatible(a, b);

    if (got != want)
        t_fail(t, __FILE__, __LINE__, "%s: compatible=%d, want %d", what,
               (int)got, (int)want);
    t->assertions++;
    /* Compatibility is symmetric; asserting it here catches one-sided
     * rules, which is the classic way this function rots. */
    if (type_compatible(b, a) != want)
        t_fail(t, __FILE__, __LINE__, "%s: NOT symmetric", what);
    t->assertions++;
}

void test_sema_type_compatible_table(TestCtx *t)
{
    TypeFix tf;
    Type *i, *u, *l, *c, *sc, *uc, *s, *us, *bl, *v, *f, *d, *ld;
    TagDecl enum_tag;
    Type *en;
    Type *p1[2], *p2[2];

    arena_init(&tf.ar);
    memset(&enum_tag, 0, sizeof(enum_tag));
    i = type_basic(TY_INT);
    u = type_basic(TY_UINT);
    l = type_basic(TY_LONG);
    c = type_basic(TY_CHAR);
    sc = type_basic(TY_SCHAR);
    uc = type_basic(TY_UCHAR);
    s = type_basic(TY_SHORT);
    us = type_basic(TY_USHORT);
    bl = type_basic(TY_BOOL);
    v = type_basic(TY_VOID);
    f = type_basic(TY_FLOAT);
    d = type_basic(TY_DOUBLE);
    ld = type_basic(TY_LDOUBLE);
    enum_tag.kind = TY_ENUM;
    enum_tag.complete = true;
    enum_tag.enum_underlying = i;
    en = type_tag(&tf.ar, &enum_tag);

    /* Basic kinds: identical or nothing. `int` and `long` are distinct
     * even at equal width — this is what keeps %ld checking honest. */
    compat_is(t, "int/int", i, i, true);
    compat_is(t, "int/unsigned", i, u, false);
    compat_is(t, "int/long", i, l, false);
    compat_is(t, "char/signed char", c, sc, false);
    compat_is(t, "char/unsigned char", c, uc, false);
    compat_is(t, "signed char/unsigned char", sc, uc, false);
    compat_is(t, "void/void", v, v, true);
    compat_is(t, "void/int", v, i, false);
    compat_is(t, "double/int", d, i, false);

    /* Qualifiers are part of type identity. */
    compat_is(t, "const int/int", Q(&tf, i, CGF_QUAL_CONST), i, false);
    compat_is(t, "const int/const int", Q(&tf, i, CGF_QUAL_CONST),
              Q(&tf, i, CGF_QUAL_CONST), true);
    compat_is(t, "const int/volatile int", Q(&tf, i, CGF_QUAL_CONST),
              Q(&tf, i, CGF_QUAL_VOLATILE), false);

    /* Pointers: compatible pointees, and IDENTICALLY qualified pointees. */
    compat_is(t, "int*/int*", P(&tf, i), P(&tf, i), true);
    compat_is(t, "int*/long*", P(&tf, i), P(&tf, l), false);
    compat_is(t, "int*/const int*", P(&tf, i),
              P(&tf, Q(&tf, i, CGF_QUAL_CONST)), false);
    compat_is(t, "const int*/const int*", P(&tf, Q(&tf, i, CGF_QUAL_CONST)),
              P(&tf, Q(&tf, i, CGF_QUAL_CONST)), true);
    compat_is(t, "int**/int**", P(&tf, P(&tf, i)), P(&tf, P(&tf, i)), true);
    compat_is(t, "int**/int*", P(&tf, P(&tf, i)), P(&tf, i), false);
    /* A const POINTER differs from a pointer to const. */
    compat_is(t, "int* const/int*", Q(&tf, P(&tf, i), CGF_QUAL_CONST),
              P(&tf, i), false);

    /* Arrays: compatible elements, and equal sizes only when BOTH know
     * one — which is what makes `int a[]; int a[10];` legal. */
    compat_is(t, "int[10]/int[10]", A(&tf, i, true, 10), A(&tf, i, true, 10),
              true);
    compat_is(t, "int[10]/int[20]", A(&tf, i, true, 10), A(&tf, i, true, 20),
              false);
    compat_is(t, "int[]/int[10]", A(&tf, i, false, 0), A(&tf, i, true, 10),
              true);
    compat_is(t, "int[]/int[]", A(&tf, i, false, 0), A(&tf, i, false, 0), true);
    compat_is(t, "int[10]/long[10]", A(&tf, i, true, 10), A(&tf, l, true, 10),
              false);
    compat_is(t, "int[2][3]/int[2][3]", A(&tf, A(&tf, i, true, 3), true, 2),
              A(&tf, A(&tf, i, true, 3), true, 2), true);
    compat_is(t, "int[2][3]/int[2][4]", A(&tf, A(&tf, i, true, 3), true, 2),
              A(&tf, A(&tf, i, true, 4), true, 2), false);
    compat_is(t, "int*[4]/int*[4]", A(&tf, P(&tf, i), true, 4),
              A(&tf, P(&tf, i), true, 4), true);

    /* Functions: an unprototyped declaration is compatible only with a
     * nonvariadic prototype whose parameter types survive the default
     * argument promotions unchanged. compat_is checks both orderings. */
    p1[0] = i;
    p1[1] = P(&tf, c);
    p2[0] = i;
    p2[1] = P(&tf, i);
    compat_is(t, "int()/int()", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, NULL, 0, false, false), true);
    compat_is(t, "int()/int(int,char*)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 2, true, false), true);
    p1[0] = c;
    compat_is(t, "int()/int(char)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), false);
    p1[0] = sc;
    compat_is(t, "int()/int(signed char)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), false);
    p1[0] = uc;
    compat_is(t, "int()/int(unsigned char)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), false);
    p1[0] = s;
    compat_is(t, "int()/int(short)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), false);
    p1[0] = us;
    compat_is(t, "int()/int(unsigned short)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), false);
    p1[0] = bl;
    compat_is(t, "int()/int(_Bool)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), false);
    p1[0] = f;
    compat_is(t, "int()/int(float)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), false);
    p1[0] = i;
    compat_is(t, "int()/int(int)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), true);
    p1[0] = u;
    compat_is(t, "int()/int(unsigned)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), true);
    p1[0] = l;
    compat_is(t, "int()/int(long)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), true);
    p1[0] = d;
    compat_is(t, "int()/int(double)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), true);
    p1[0] = ld;
    compat_is(t, "int()/int(long double)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), true);
    p1[0] = P(&tf, c);
    compat_is(t, "int()/int(char*)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), true);
    p1[0] = en;
    compat_is(t, "int()/int(enum)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), true);
    p1[0] = i;
    compat_is(t, "int()/int(int,...)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, true), false);
    compat_is(t, "int()/int(void)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, NULL, 0, true, false), true);
    p1[0] = Q(&tf, i, CGF_QUAL_CONST);
    compat_is(t, "int()/int(const int)", F(&tf, i, NULL, 0, false, false),
              F(&tf, i, p1, 1, true, false), true);
    p1[0] = f;
    compat_is(t, "K&R float definition/float prototype", KRF(&tf, i, p1, 1),
              F(&tf, i, p1, 1, true, false), false);
    p2[0] = d;
    compat_is(t, "K&R float definition/double prototype", KRF(&tf, i, p1, 1),
              F(&tf, i, p2, 1, true, false), true);
    p1[0] = c;
    p2[0] = i;
    compat_is(t, "K&R char definition/int prototype", KRF(&tf, i, p1, 1),
              F(&tf, i, p2, 1, true, false), true);
    compat_is(t, "empty-list definition/int prototype", ODF(&tf, i),
              F(&tf, i, p2, 1, true, false), false);
    compat_is(t, "empty-list definition/void prototype", ODF(&tf, i),
              F(&tf, i, NULL, 0, true, false), true);
    compat_is(t, "K&R definition/variadic prototype", KRF(&tf, i, p2, 1),
              F(&tf, i, p2, 1, true, true), false);
    compat_is(t, "K&R count mismatch", KRF(&tf, i, p2, 1),
              F(&tf, i, p1, 2, true, false), false);
    p1[0] = i;
    p1[1] = P(&tf, c);
    compat_is(t, "int(int,char*)/int(int,char*)", F(&tf, i, p1, 2, true, false),
              F(&tf, i, p1, 2, true, false), true);
    compat_is(t, "int(int,char*)/int(int,int*)", F(&tf, i, p1, 2, true, false),
              F(&tf, i, p2, 2, true, false), false);
    compat_is(t, "int(void)/int(int)", F(&tf, i, NULL, 0, true, false),
              F(&tf, i, p1, 1, true, false), false);
    compat_is(t, "int(int)/int(int,...)", F(&tf, i, p1, 1, true, false),
              F(&tf, i, p1, 1, true, true), false);
    compat_is(t, "int(void)/long(void)", F(&tf, i, NULL, 0, true, false),
              F(&tf, l, NULL, 0, true, false), false);
    /* 6.7.6.3p15: a parameter's TOP-LEVEL qualifiers are ignored for
     * compatibility — `void f(int *const)` and `void f(int *)` are the
     * same function. Found by the c-testsuite differential. */
    p2[0] = Q(&tf, P(&tf, i), CGF_QUAL_CONST);
    p1[0] = P(&tf, i);
    compat_is(t, "f(int*const)/f(int*)", F(&tf, v, p2, 1, true, false),
              F(&tf, v, p1, 1, true, false), true);

    /* A poisoned type is compatible with everything, so an already
     * -diagnosed construct never produces a second complaint. */
    compat_is(t, "error/int", type_basic(TY_ERROR), i, true);
    compat_is(t, "error/int*", type_basic(TY_ERROR), P(&tf, i), true);

    arena_free_all(&tf.ar);
}

void test_sema_type_composite_table(TestCtx *t)
{
    TypeFix tf;
    Type *i = type_basic(TY_INT);
    Type *c;
    Type *p1[2];

    arena_init(&tf.ar);

    /* 6.2.7p3: the size comes from whichever declaration has one. */
    c = type_composite(&tf.ar, A(&tf, i, false, 0), A(&tf, i, true, 10));
    T_ASSERT(t, c->kind == TY_ARRAY);
    T_ASSERT(t, c->has_size);
    T_ASSERT_EQ_INT(t, (int)c->size, 10);

    c = type_composite(&tf.ar, A(&tf, i, true, 10), A(&tf, i, false, 0));
    T_ASSERT(t, c->has_size);
    T_ASSERT_EQ_INT(t, (int)c->size, 10);

    c = type_composite(&tf.ar, A(&tf, i, false, 0), A(&tf, i, false, 0));
    T_ASSERT(t, !c->has_size);

    /* An unprototyped declaration plus a prototype yields the PROTOTYPE. */
    p1[0] = i;
    p1[1] = P(&tf, type_basic(TY_CHAR));
    c = type_composite(&tf.ar, F(&tf, i, NULL, 0, false, false),
                       F(&tf, i, p1, 2, true, false));
    T_ASSERT(t, c->kind == TY_FUNC);
    T_ASSERT(t, c->has_proto);
    T_ASSERT_EQ_INT(t, (int)c->nparams, 2);

    c = type_composite(&tf.ar, F(&tf, i, p1, 2, true, false),
                       F(&tf, i, NULL, 0, false, false));
    T_ASSERT(t, c->has_proto);
    T_ASSERT_EQ_INT(t, (int)c->nparams, 2);

    /* Neither prototyped: still unprototyped. */
    c = type_composite(&tf.ar, F(&tf, i, NULL, 0, false, false),
                       F(&tf, i, NULL, 0, false, false));
    T_ASSERT(t, !c->has_proto);

    p1[0] = type_basic(TY_FLOAT);
    c = type_composite(&tf.ar, KRF(&tf, i, p1, 1),
                       F(&tf, i, NULL, 0, false, false));
    T_ASSERT(t, !c->has_proto);
    T_ASSERT(t, c->old_style_definition);
    T_ASSERT(t, c->kr_definition);
    T_ASSERT_EQ_INT(t, (int)c->nold_style_params, 1);
    T_ASSERT(t, c->old_style_params[0] == type_basic(TY_FLOAT));

    /* Composites recurse through pointers and arrays. */
    c = type_composite(&tf.ar, P(&tf, A(&tf, i, false, 0)),
                       P(&tf, A(&tf, i, true, 5)));
    T_ASSERT(t, c->kind == TY_PTR);
    T_ASSERT(t, c->base->has_size);
    T_ASSERT_EQ_INT(t, (int)c->base->size, 5);

    arena_free_all(&tf.ar);
}

void test_sema_type_to_str(TestCtx *t)
{
    TypeFix tf;
    Type *i = type_basic(TY_INT);
    Type *ch = type_basic(TY_CHAR);

    arena_init(&tf.ar);
    T_ASSERT_EQ_STR(t, type_to_str(&tf.ar, i), "int");
    T_ASSERT_EQ_STR(t, type_to_str(&tf.ar, P(&tf, i)), "int *");
    /* Qualifiers on a DERIVED type render after it: a const pointer to
     * const char is not "const const char *". */
    T_ASSERT_EQ_STR(
        t,
        type_to_str(&tf.ar,
                    Q(&tf, P(&tf, Q(&tf, ch, CGF_QUAL_CONST)), CGF_QUAL_CONST)),
        "const char * const");
    /* Arrays render OUTERMOST bound first, as C writes them. */
    T_ASSERT_EQ_STR(t, type_to_str(&tf.ar, A(&tf, A(&tf, i, true, 3), true, 2)),
                    "int [2] [3]");
    T_ASSERT_EQ_STR(t, type_to_str(&tf.ar, A(&tf, i, false, 0)), "int []");
    T_ASSERT_EQ_STR(t, type_to_str(&tf.ar, F(&tf, i, NULL, 0, true, false)),
                    "int (void)");
    T_ASSERT_EQ_STR(t, type_to_str(&tf.ar, F(&tf, i, NULL, 0, false, false)),
                    "int (unspecified)");
    arena_free_all(&tf.ar);
}

/* --- the linkage matrix (6.2.2) ------------------------------------------ */

static Symbol *lookup(SemaFix *f, const char *name)
{
    const char *interned = intern_str(&f->in, intern_cstr(&f->in, name));

    return scope_lookup(f->sema.file_scope, interned, NS_ORDINARY);
}

static void linkage_is(TestCtx *t, const char *src, const char *name,
                       Linkage want, const char *label)
{
    SemaFix f;
    Symbol *sym;

    run_sema(&f, src, STD_C17);
    sym = lookup(&f, name);
    if (!sym)
        t_fail(t, __FILE__, __LINE__, "%s: '%s' not found", label, name);
    else if (sym->linkage != want)
        t_fail(t, __FILE__, __LINE__, "%s: linkage %d, want %d", label,
               (int)sym->linkage, (int)want);
    t->assertions++;
    sfix_free(&f);
}

void test_sema_linkage_matrix(TestCtx *t)
{
    /* One case per row of the 6.2.2 table. */
    linkage_is(t, "static int x;\n", "x", LINK_INTERNAL,
               "file-scope static object");
    linkage_is(t, "static int f(void);\n", "f", LINK_INTERNAL,
               "file-scope static function");
    linkage_is(t, "int f(void);\n", "f", LINK_EXTERNAL,
               "file-scope function, no storage class");
    linkage_is(t, "extern int f(void);\n", "f", LINK_EXTERNAL,
               "file-scope extern function");
    linkage_is(t, "int x;\n", "x", LINK_EXTERNAL,
               "file-scope object, no storage class");
    linkage_is(t, "extern int x;\n", "x", LINK_EXTERNAL,
               "file-scope extern object, no prior");
    linkage_is(t, "static int x;\nextern int x;\n", "x", LINK_INTERNAL,
               "file-scope extern after static picks up internal");
    /* Typedefs, enum constants and parameters have no linkage. */
    linkage_is(t, "typedef int T;\n", "T", LINK_NONE, "typedef");
    linkage_is(t, "enum E { A };\n", "A", LINK_NONE, "enum constant");
}

void test_sema_linkage_p4_block_extern(TestCtx *t)
{
    SemaFix f;
    Symbol *sym;

    /* THE p4 horror: a block-scope `extern` takes the linkage of a PRIOR
     * VISIBLE declaration with linkage. The file-scope static wins, so the
     * inner x has INTERNAL linkage — not external. */
    run_sema(&f, "static int x;\nvoid g(void) { extern int x; }\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sym = lookup(&f, "x");
    T_ASSERT(t, sym != NULL);
    T_ASSERT_EQ_INT(t, (int)sym->linkage, (int)LINK_INTERNAL);
    sfix_free(&f);

    /* With no prior declaration, the same block-scope extern is external. */
    run_sema(&f, "void g(void) { extern int y; }\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    /* A block-scope object without `extern` has NO linkage. */
    run_sema(&f, "void g(void) { int z; (void)z; }\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    /* SEMA-H-04: an automatic declaration and a block-scope extern in the
     * SAME scope denote entities with different linkage. Reject both orders
     * rather than merging their compatible types. */
    run_sema(&f, "void g(void) { int a; extern int a; }\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    run_sema(&f, "void g(void) { extern int a; int a; }\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* A no-linkage declaration in an OUTER block is merely shadowed by the
     * nested extern; no same-scope merge occurs, matching GCC. */
    run_sema(&f, "void g(void) { int a; { extern int a; a = 1; } }\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    /* Linked declarations still inherit and merge normally, including
     * composite array completion. */
    run_sema(&f,
             "static int a[10];\n"
             "void g(void) { extern int a[]; extern int a[10]; }\n",
             STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);
}

void test_sema_linkage_p7_conflict(TestCtx *t)
{
    SemaFix f;

    /* 6.2.2p7: one identifier with both internal and external linkage in a
     * TU is undefined. gcc errors when `static` FOLLOWS non-static... */
    run_sema(&f, "int x;\nstatic int x;\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* ...and ACCEPTS the reverse, because there the extern picks up
     * internal linkage from the prior declaration. Matching both halves
     * is the point: rejecting the second would break real code. */
    run_sema(&f, "static int x;\nextern int x;\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    run_sema(&f, "static int f(void);\nint f(void) { return 0; }\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0); /* extern-after-static keeps internal */
    sfix_free(&f);
}

/* --- namespaces and scopes ----------------------------------------------- */

void test_sema_namespaces(TestCtx *t)
{
    SemaFix f;

    /* Tags and ordinary identifiers are SEPARATE namespaces, so a struct
     * named S and a variable named S coexist. */
    run_sema(&f, "struct S { int a; };\nint S;\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    /* struct, union and enum share ONE tag namespace, so these collide. */
    run_sema(&f, "struct T { int a; };\nenum T { X };\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    run_sema(&f, "struct U { int a; };\nunion U { int b; };\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* Members are per-struct, so a member may share a name with anything
     * outside — and only a duplicate within the SAME struct is an error. */
    run_sema(&f, "typedef int m;\nstruct A { int m; };\nstruct B { int m; };\n",
             STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    run_sema(&f, "struct C { int m; char m; };\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* Enum constants live in the ORDINARY namespace, so they collide with
     * variables. */
    run_sema(&f, "enum E { dup };\nint dup;\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);
}

void test_sema_tag_scoping(TestCtx *t)
{
    SemaFix f;

    /* A self-referential struct: the tag must be visible inside its own
     * body, as an incomplete type. */
    run_sema(&f, "struct S; struct S { int x; struct S *next; };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    /* An inner block's definition is a FRESH tag, not a redefinition. */
    run_sema(&f,
             "int f(void) { struct T { int x; } a; { struct T { int y; } b;"
             " (void)b; } (void)a; return 0; }\n",
             STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    /* Two definitions in the SAME scope is a redefinition. */
    run_sema(&f, "struct R { int x; };\nstruct R { int y; };\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* A tag first named inside a parameter list dies at the ')' — gcc
     * warns because no caller can ever name that type. */
    run_sema(&f, "void g(struct Hidden *p);\n", STD_C17);
    T_ASSERT(t, f.warnings >= 1);
    sfix_free(&f);

    /* A completion in the same scope completes the existing tag in place;
     * a use before completion is legal through a pointer. */
    run_sema(&f, "struct L; struct L *p; struct L { int v; };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);
}

void test_sema_incomplete_types(TestCtx *t)
{
    SemaFix f;

    /* A member of incomplete type could never be laid out. */
    run_sema(&f, "struct I;\nstruct H { struct I m; };\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* ...but a POINTER to one is fine. */
    run_sema(&f, "struct I;\nstruct H { struct I *m; };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    /* An array of incomplete elements likewise. */
    run_sema(&f, "struct I;\nstruct I arr[4];\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* A union may contain a FAM-bearing struct in ISO C. GCC extends that
     * recursively to struct members and array elements, whose fixed layout
     * is still well-defined because the flexible tail contributes no size. */
    run_sema(&f,
             "struct F { int head; int tail[]; };\n"
             "union U { struct F f; long fallback; };\n"
             "struct O { union U u; int end; };\n"
             "struct A { struct F array[2]; };\n"
             "struct F array[2];\n",
             STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    {
        const char *fname = intern_str(&f.in, intern_cstr(&f.in, "F"));
        const char *uname = intern_str(&f.in, intern_cstr(&f.in, "U"));
        const char *oname = intern_str(&f.in, intern_cstr(&f.in, "O"));
        const char *aname = intern_str(&f.in, intern_cstr(&f.in, "A"));
        Symbol *ftag = scope_lookup(f.sema.scope, fname, NS_TAG);
        Symbol *utag = scope_lookup(f.sema.scope, uname, NS_TAG);
        Symbol *otag = scope_lookup(f.sema.scope, oname, NS_TAG);
        Symbol *atag = scope_lookup(f.sema.scope, aname, NS_TAG);

        T_ASSERT(t, ftag && ftag->type->tag->has_fam);
        T_ASSERT(t, ftag && ftag->type->tag->contains_fam);
        T_ASSERT(t, utag && !utag->type->tag->has_fam);
        T_ASSERT(t, utag && utag->type->tag->contains_fam);
        T_ASSERT(t, otag && !otag->type->tag->has_fam);
        T_ASSERT(t, otag && otag->type->tag->contains_fam);
        T_ASSERT(t, atag && !atag->type->tag->has_fam);
        T_ASSERT(t, atag && atag->type->tag->contains_fam);
    }
    sfix_free(&f);

    /* void objects are never definable. */
    run_sema(&f, "void v;\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* An unsized array is COMPLETED by its initializer (6.7.9p22). */
    run_sema(&f, "int a[] = { 1, 2, 3 };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    {
        Symbol *sym = lookup(&f, "a");
        T_ASSERT(t, sym && sym->type->kind == TY_ARRAY);
        T_ASSERT(t, sym->type->has_size);
        T_ASSERT_EQ_INT(t, (int)sym->type->size, 3);
    }
    sfix_free(&f);

    /* A designator moves the cursor, so the size follows the highest
     * index reached, not the item count. */
    run_sema(&f, "int b[] = { [5] = 1 };\n", STD_C17);
    {
        Symbol *sym = lookup(&f, "b");
        T_ASSERT(t, sym && sym->type->has_size);
        T_ASSERT_EQ_INT(t, (int)sym->type->size, 6);
    }
    sfix_free(&f);

    /* A GNU range completes an unsized array from its inclusive upper
     * endpoint. Chained ranges expand as a Cartesian product while retaining
     * the ordinary current-object rules at each array level. */
    run_sema(&f,
             "int r[] = { [2 ... 5] = 1 };\n"
             "int m[][4] = { [0 ... 1][1 ... 3] = 7 };\n",
             STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    {
        Symbol *r = lookup(&f, "r");
        Symbol *m = lookup(&f, "m");

        T_ASSERT(t, r && r->type->has_size);
        T_ASSERT_EQ_INT(t, (int)r->type->size, 6);
        T_ASSERT(t, m && m->type->has_size);
        T_ASSERT_EQ_INT(t, (int)m->type->size, 2);
    }
    sfix_free(&f);

    run_sema(&f, "int bad[4] = { [3 ... 1] = 0 };\n", STD_GNU17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    run_sema(&f, "int bad[4] = { [1 ... 4] = 0 };\n", STD_GNU17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* A string literal completes a char array, terminator included. */
    run_sema(&f, "char s[] = \"abc\";\n", STD_C17);
    {
        Symbol *sym = lookup(&f, "s");
        T_ASSERT(t, sym && sym->type->has_size);
        T_ASSERT_EQ_INT(t, (int)sym->type->size, 4);
    }
    sfix_free(&f);

    /* Brace elision advances through the element's subobjects before it
     * advances the incomplete OUTER array. Counting syntax items made this
     * six-element array instead of two even though initialization stored the
     * values into two complete records. */
    run_sema(&f,
             "struct P { int a[2]; int b; };\n"
             "struct P flat[] = { 1, 2, 3, 4, 5, 6 };\n"
             "struct P braced[] = { { { 1, 2 }, 3 }, "
             "{ { 4, 5 }, 6 } };\n"
             "struct P designated[] = { [3] = { { 1, 2 }, 3 } };\n",
             STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    {
        Symbol *flat = lookup(&f, "flat");
        Symbol *braced = lookup(&f, "braced");
        Symbol *designated = lookup(&f, "designated");

        T_ASSERT(t, flat && flat->type->has_size);
        T_ASSERT_EQ_INT(t, (int)flat->type->size, 2);
        T_ASSERT(t, braced && braced->type->has_size);
        T_ASSERT_EQ_INT(t, (int)braced->type->size, 2);
        T_ASSERT(t, designated && designated->type->has_size);
        T_ASSERT_EQ_INT(t, (int)designated->type->size, 4);
    }
    sfix_free(&f);

    /* Aggregate-valued items consume one complete element. This is why
     * declaration completion runs after initializer typing: syntax alone
     * cannot tell that an identifier, call, or conditional has record type. */
    run_sema(&f,
             "struct P { int a[2]; int b; };\n"
             "void f(void) {\n"
             "  struct P one = { { 1, 2 }, 3 };\n"
             "  struct P whole[] = { one, (struct P){ { 4, 5 }, 6 } };\n"
             "  int size[(sizeof whole / sizeof whole[0]) == 2 ? 1 : -1];\n"
             "  (void)size;\n"
             "}\n",
             STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    /* GNU static FAM initialization enlarges emitted STORAGE without
     * completing or otherwise changing the declared struct type. */
    run_sema(&f,
             "struct S { int head; int tail[]; };\n"
             "struct S direct = { 1, { 2 } };\n"
             "struct S sparse = { .tail = { [2] = 7 } };\n",
             STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    {
        Symbol *direct = lookup(&f, "direct");
        Symbol *sparse = lookup(&f, "sparse");

        T_ASSERT(t, direct && direct->type->kind == TY_STRUCT);
        T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, direct->type).size, 4);
        T_ASSERT_EQ_INT(t, (int)direct->init_storage_size, 8);
        T_ASSERT(t, sparse && sparse->type == direct->type);
        T_ASSERT_EQ_INT(t, (int)sparse->init_storage_size, 16);
    }
    sfix_free(&f);

    /* The extension is limited to static storage, matching GCC. */
    run_sema(&f,
             "struct S { int head; int tail[]; };\n"
             "void f(void) { struct S bad = { 1, { 2 } }; (void)bad; }\n",
             STD_GNU17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* A block-scope static has static storage duration and is accepted. */
    run_sema(&f,
             "struct S { int head; int tail[]; };\n"
             "void f(void) { static struct S ok = { 1, { 2, 3 } }; "
             "(void)ok; }\n",
             STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    /* GCC does not allocate a recursively buried flexible tail. The GNU
     * containment extension permits the TYPE, but not an initializer that
     * tries to populate that nested array. */
    run_sema(&f,
             "struct F { int head; int tail[]; };\n"
             "struct O { struct F f; };\n"
             "static struct O braced = { { 1, { 2 } } };\n"
             "static struct O flat = { 1, 2 };\n"
             "static struct O designated = { .f.tail = { [2] = 3 } };\n",
             STD_GNU17);
    T_ASSERT(t, f.errors >= 3);
    sfix_free(&f);
}

void test_sema_redeclaration(TestCtx *t)
{
    SemaFix f;
    Symbol *sym;

    /* Composite of array types: the size comes from whichever has one. */
    run_sema(&f, "int a[];\nint a[10];\nint a[];\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sym = lookup(&f, "a");
    T_ASSERT(t, sym && sym->type->has_size);
    T_ASSERT_EQ_INT(t, (int)sym->type->size, 10);
    sfix_free(&f);

    /* Composite of function types keeps the prototype. */
    run_sema(&f, "int f();\nint f(int, char *);\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sym = lookup(&f, "f");
    T_ASSERT(t, sym && sym->type->has_proto);
    T_ASSERT_EQ_INT(t, (int)sym->type->nparams, 2);
    sfix_free(&f);

    /* Incompatible redeclarations are errors citing both locations. */
    run_sema(&f, "int x;\nlong x;\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* Two definitions is a redefinition. */
    run_sema(&f, "int y = 1;\nint y = 2;\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);

    /* Several TENTATIVE definitions are fine (6.9.2p2). */
    run_sema(&f, "int z;\nint z;\nint z;\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sym = lookup(&f, "z");
    T_ASSERT(t, sym && sym->tentative);
    sfix_free(&f);

    /* A real definition ends tentativeness. */
    run_sema(&f, "int w;\nint w = 5;\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sym = lookup(&f, "w");
    T_ASSERT(t, sym && !sym->tentative && sym->defined);
    sfix_free(&f);

    /* Block-scope no-linkage redeclaration is an error. */
    run_sema(&f, "void g(void) { int a; int a; }\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);
}

void test_sema_typedef_redeclaration(TestCtx *t)
{
    SemaFix f;

    /* C11 allows redeclaring a typedef to the SAME type... */
    run_sema(&f, "typedef int T;\ntypedef int T;\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    sfix_free(&f);

    /* ...which C99 forbade, so there it pedwarns rather than passing
     * silently. */
    run_sema(&f, "typedef int T;\ntypedef int T;\n", STD_C99);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, f.warnings >= 1);
    sfix_free(&f);

    /* A different type is an error in every mode. */
    run_sema(&f, "typedef int T;\ntypedef long T;\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);
}

void test_sema_enums(TestCtx *t)
{
    SemaFix f;
    Symbol *sym;

    /* Values default to previous + 1; constants have type int. */
    run_sema(&f, "enum E { A, B = 5, C };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sym = lookup(&f, "A");
    T_ASSERT(t, sym && sym->enum_value == 0);
    T_ASSERT(t, sym->type == type_basic(TY_INT));
    sym = lookup(&f, "B");
    T_ASSERT(t, sym && sym->enum_value == 5);
    sym = lookup(&f, "C");
    T_ASSERT(t, sym && sym->enum_value == 6);
    sfix_free(&f);

    /* gcc's underlying-type ladder, all four buckets. */
    run_sema(&f, "enum A1 { a1 = 1 };\n", STD_C17);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "A1")), NS_TAG);
    T_ASSERT(t, sym && sym->tag->enum_underlying == type_basic(TY_INT));
    sfix_free(&f);

    run_sema(&f, "enum A2 { a2 = 3000000000 };\n", STD_C17);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "A2")), NS_TAG);
    T_ASSERT(t, sym && sym->tag->enum_underlying == type_basic(TY_UINT));
    sfix_free(&f);

    run_sema(&f, "enum A3 { a3 = 5000000000 };\n", STD_C17);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "A3")), NS_TAG);
    T_ASSERT(t, sym && sym->tag->enum_underlying == type_basic(TY_LONG));
    sfix_free(&f);

    /* A negative enumerator forces a SIGNED choice even when the positive
     * range would fit unsigned. */
    run_sema(&f, "enum A4 { n1 = -1, n2 = 3000000000 };\n", STD_C17);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "A4")), NS_TAG);
    T_ASSERT(t, sym && sym->tag->enum_underlying == type_basic(TY_LONG));
    sfix_free(&f);

    /* An out-of-int enumerator takes the ENUM's type — gcc's extension,
     * and observable as sizeof. */
    run_sema(&f, "enum A5 { small = 1, big = 3000000000 };\n", STD_C17);
    sym = lookup(&f, "big");
    T_ASSERT(t, sym && sym->type == type_basic(TY_UINT));
    sym = lookup(&f, "small");
    T_ASSERT(t, sym && sym->type == type_basic(TY_INT));
    sfix_free(&f);

    /* ...and pedwarns only under -pedantic, matching gcc. */
    run_sema(&f, "enum A6 { z = 3000000000 };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    sfix_free(&f);

    /* An enumerator expression may reference earlier constants. */
    run_sema(&f, "enum A7 { p = 2, q = p * 3, r = q + 1 };\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sym = lookup(&f, "r");
    T_ASSERT(t, sym && sym->enum_value == 7);
    sfix_free(&f);

    /* Duplicate enumerators collide in the ordinary namespace. */
    run_sema(&f, "enum A8 { same, same };\n", STD_C17);
    T_ASSERT(t, f.errors >= 1);
    sfix_free(&f);
}

void test_sema_enum_mode_attributes(TestCtx *t)
{
    SemaFix f;
    Symbol *sym;
    Symbol *tag;

    run_sema(&f,
             "typedef enum { U0, U255 = 255 } "
             "__attribute__((mode(QI))) U8;\n" /* check_bans allow */
             "typedef enum { N128 = -128, P127 = 127 } "
             "__attribute__((mode(QI))) I8;\n" /* check_bans allow */
             "typedef enum Direct { D0, D1 } "
             "__attribute__((mode(HI))) Direct;\n" /* check_bans allow */
             "typedef enum Suffix { S0, S1 } Suffix "
             "__attribute__((mode(QI)));\n" /* check_bans allow */
             "__attribute__((mode(QI))) "   /* check_bans allow */
             "enum Leading { L0, L1 } leading;\n",
             STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sym = lookup(&f, "U8");
    T_ASSERT(t, sym && sym->type && sym->type->kind == TY_ENUM);
    T_ASSERT(t, sym && type_enum_underlying(sym->type) == type_basic(TY_UCHAR));
    sym = lookup(&f, "I8");
    T_ASSERT(t, sym && type_enum_underlying(sym->type) == type_basic(TY_SCHAR));
    tag = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "Direct")), NS_TAG);
    T_ASSERT(t, tag && tag->tag &&
                    tag->tag->enum_underlying == type_basic(TY_USHORT));
    tag = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "Suffix")), NS_TAG);
    T_ASSERT(t, tag && tag->tag &&
                    tag->tag->enum_underlying == type_basic(TY_INT));
    sym = lookup(&f, "Suffix");
    T_ASSERT(t, sym && type_enum_underlying(sym->type) == type_basic(TY_UCHAR));
    tag =
        scope_lookup(f.sema.file_scope,
                     intern_str(&f.in, intern_cstr(&f.in, "Leading")), NS_TAG);
    T_ASSERT(t, tag && tag->tag &&
                    tag->tag->enum_underlying == type_basic(TY_INT));
    sym = lookup(&f, "leading");
    T_ASSERT(t, sym && type_enum_underlying(sym->type) == type_basic(TY_UCHAR));
    sfix_free(&f);

    /* A mode on a use of an existing tag is a distinct enum view. It must
     * not mutate the tag's ordinary representation, while repeated uses of
     * the same mode remain compatible with one another. */
    run_sema(&f,
             "enum Base { B0, B1 };\n"
             "typedef enum Base "
             "__attribute__((mode(QI))) " /* check_bans allow */
             "Narrow1;\n"
             "typedef enum Base "
             "__attribute__((mode(QI))) " /* check_bans allow */
             "Narrow2;\n"
             "typedef enum Base "
             "__attribute__((mode(HI))) " /* check_bans allow */
             "Wide;\n",
             STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    tag = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "Base")), NS_TAG);
    T_ASSERT(t, tag && tag->tag &&
                    tag->tag->enum_underlying == type_basic(TY_INT));
    sym = lookup(&f, "Narrow1");
    T_ASSERT(t, sym && type_enum_underlying(sym->type) == type_basic(TY_UCHAR));
    {
        Symbol *same = lookup(&f, "Narrow2");
        Symbol *wide = lookup(&f, "Wide");

        T_ASSERT(t, same && type_compatible(sym->type, same->type));
        T_ASSERT(t, wide && !type_compatible(sym->type, wide->type));
        T_ASSERT(t, tag && !type_compatible(sym->type, tag->type));
    }
    sfix_free(&f);

    run_sema(&f,
             "typedef enum { TOO_HIGH = 256 } "
             "__attribute__((mode(QI))) TooHigh;\n" /* check_bans allow */
             "typedef enum { TOO_LOW = -129 } "
             "__attribute__((mode(QI))) TooLow;\n", /* check_bans allow */
             STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 2);
    sfix_free(&f);
}

void test_sema_extended_alignment_limit(TestCtx *t)
{
    SemaFix f;

    T_ASSERT_EQ_INT(t, CGF_MAX_OBJECT_ALIGN, 16777216);
    run_sema(&f,
             "int f(int n) { _Alignas(16777216) int fixed; "
             "_Alignas(16777216) int vla[n]; return fixed + vla[0]; }\n",
             STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    sfix_free(&f);

    run_sema(&f, "_Alignas(33554432) int too_large;\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    sfix_free(&f);

    run_sema(&f, "_Alignas(4294967296LL) int above_u32;\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    sfix_free(&f);

    run_sema(&f,
             "int x __attribute__((aligned(33554432)));\n", /* check_bans allow:
                                                               compiler input,
                                                               not host C */
             STD_GNU17);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    sfix_free(&f);
}

/* --- spell-check --------------------------------------------------------- */

void test_sema_dlev_distance(TestCtx *t)
{
    unsigned d;

    T_ASSERT_EQ_INT(t, (int)dlev_distance("abc", 3, "abc", 3, 2), 0);
    T_ASSERT_EQ_INT(t, (int)dlev_distance("abc", 3, "abd", 3, 2), 1);
    T_ASSERT_EQ_INT(t, (int)dlev_distance("abc", 3, "ab", 2, 2), 1);
    T_ASSERT_EQ_INT(t, (int)dlev_distance("ab", 2, "abc", 3, 2), 1);
    /* Transposition is ONE edit under Damerau, not two — which is the
     * whole reason to use it, since swapped letters are a common typo. */
    T_ASSERT_EQ_INT(t, (int)dlev_distance("file", 4, "flie", 4, 2), 1);
    T_ASSERT_EQ_INT(t, (int)dlev_distance("abcd", 4, "badc", 4, 2), 2);
    /* Beyond the cap the exact value is never interesting. */
    T_ASSERT(t, dlev_distance("abc", 3, "xyzzy", 5, 2) > 2);
    T_ASSERT(t, dlev_distance("", 0, "abcdef", 6, 2) > 2);

    /* Short names use a tighter bound: at distance 2 a three-letter name
     * reaches nearly everything and the suggestion becomes noise. */
    T_ASSERT(t, dlev_is_suggestion("foo", 3, "fob", 3, &d));
    T_ASSERT(t, !dlev_is_suggestion("foo", 3, "bar", 3, &d));
    T_ASSERT(t, dlev_is_suggestion("uint32_r", 8, "uint32_t", 8, &d));
    /* Identical is never a suggestion. */
    T_ASSERT(t, !dlev_is_suggestion("same", 4, "same", 4, &d));
}
