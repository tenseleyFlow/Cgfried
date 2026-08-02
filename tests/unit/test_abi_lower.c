#include <string.h>

#include "lower/lower.h"
#include "parse/parse.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* ABI-lowering units (Sprint 19): the classification->rewrite table over
 * the Sprint 14 torture shapes, the va_start field constants, and the
 * va_arg expansion shapes. End-to-end through the printed IR — the same
 * text Sprint 23's codegen walks. */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    Sema sema;
    DiagCtx *dc;
    int errors;
    IrModule *m;
    Buf text;
} AbiFix;

static void abi_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    AbiFix *f = user;

    (void)dc;
    if (d->level >= DIAG_ERROR)
        f->errors++;
}

VEC_DECL(PpVecA, PpToken);

static bool run_abi(AbiFix *f, const char *src)
{
    DiagSink sink;
    SourceFile *sf;
    PpVecA pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec target;
    AstNode *tu;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = abi_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, f->dc, &f->in);

    memset(&lang, 0, sizeof(lang));
    lang.std = STD_C17;
    lang.warnings = warn_ctx_new(&f->arena, f->dc);
    f->pp.warn = lang.warnings;
    target.kind = CGF_TARGET_X86_64_LINUX_GNU;

    sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t))
        PpVecA_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, target, &f->arena);
    PpVecA_free(&pv);

    parse_init(&f->ps, &tl, &f->pp, f->dc, &f->arena, &lang);
    tu = parse_translation_unit(&f->ps);
    sema_init(&f->sema, &f->arena, f->dc, &f->in, &lang, target);
    sema_run(&f->sema, tu);
    if (f->errors)
        return false;
    f->m = lower_translation_unit(&f->arena, f->dc, &f->sema, tu);
    if (!f->m)
        return false;
    buf_init(&f->text);
    ir_print_module_buf(&f->text, f->m);
    buf_push_u8(&f->text, 0);
    return true;
}

static void abi_free(AbiFix *f)
{
    if (f->text.data)
        buf_free(&f->text);
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

static const char *atxt(AbiFix *f)
{
    return (const char *)f->text.data;
}

static int acount(const char *hay, const char *needle)
{
    int n = 0;
    size_t len = strlen(needle);

    while ((hay = strstr(hay, needle)) != NULL) {
        n++;
        hay += len;
    }
    return n;
}

void test_abi_classification_table(TestCtx *t)
{
    AbiFix f;

    /* The >= 12 pinned cases, end to end: each callee signature and each
     * call site carries the classified shape. */
    T_ASSERT(t, run_abi(&f,
                        "struct II { int a, b; };\n"   /* 8B: 1 INT 8byte */
                        "struct LL { long a, b; };\n"  /* 16B: pair II */
                        "struct SD { double d; };\n"   /* 8B: 1 SSE */
                        "struct FF { float a, b; };\n" /* 8B: 1 SSE packed */
                        "struct DI { double d; int i; };\n"   /* pair SI */
                        "struct ID { int i; double d; };\n"   /* pair IS */
                        "struct SS4 { float a, b, c, d; };\n" /* pair SS */
                        "struct BIG { char c[17]; };\n"       /* MEMORY */
                        "struct ARR { int a[4]; };\n" /* pair II via array */
                        "struct XLD { long double ld; };\n" /* f80: MEMORY */
                        "struct II g_ii; struct LL g_ll; struct SD g_sd;\n"
                        "struct FF g_ff; struct DI g_di; struct ID g_id;\n"
                        "struct SS4 g_ss; struct BIG g_big; struct ARR g_arr;\n"
                        "struct XLD g_xld;\n"
                        "struct II r_ii(void) { return g_ii; }\n"
                        "struct LL r_ll(void) { return g_ll; }\n"
                        "struct SD r_sd(void) { return g_sd; }\n"
                        "struct DI r_di(void) { return g_di; }\n"
                        "struct ID r_id(void) { return g_id; }\n"
                        "struct SS4 r_ss(void) { return g_ss; }\n"
                        "struct BIG r_big(void) { return g_big; }\n"
                        "struct XLD r_xld(void) { return g_xld; }\n"
                        "void t_ii(struct II x); void t_ll(struct LL x);\n"
                        "void t_ff(struct FF x); void t_big(struct BIG x);\n"
                        "void t_arr(struct ARR x); void t_xld(struct XLD x);\n"
                        "long double t_ld(long double v);\n"
                        "void calls(void) {\n"
                        "  t_ii(g_ii); t_ll(g_ll); t_ff(g_ff); t_big(g_big);\n"
                        "  t_arr(g_arr); t_xld(g_xld); t_ld(1.0L);\n"
                        "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));

    /* Returns. */
    T_ASSERT(t, strstr(atxt(&f), "func i64 @r_ii()") != NULL);
    T_ASSERT(t,
             strstr(atxt(&f), "func void @r_ll(ptr %0) abi(pair_ii)") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "func f64 @r_sd()") != NULL);
    T_ASSERT(t,
             strstr(atxt(&f), "func void @r_di(ptr %0) abi(pair_si)") != NULL);
    T_ASSERT(t,
             strstr(atxt(&f), "func void @r_id(ptr %0) abi(pair_is)") != NULL);
    T_ASSERT(t,
             strstr(atxt(&f), "func void @r_ss(ptr %0) abi(pair_ss)") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "func void @r_big(ptr %0) abi(sret)") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "func void @r_xld(ptr %0) abi(sret)") != NULL);

    /* Argument shapes at the call sites. */
    T_ASSERT(t, strstr(atxt(&f), "call void @t_ii(i64 %") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "call void @t_ll(i64 %") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "call void @t_ff(f64 %") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "byval(17)") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "byval(16)") != NULL); /* the f80 one */
    T_ASSERT(t, strstr(atxt(&f), "call f80 @t_ld(f80 0x") != NULL);
    /* t_arr: two INTEGER eightbytes from one array member. */
    T_ASSERT(t, acount(atxt(&f), "call void @t_arr(i64 %") == 1);
    abi_free(&f);
}

void test_abi_two_eightbyte_reassembly(TestCtx *t)
{
    AbiFix f;

    /* A pair param arrives as scalars and the prologue reassembles the
     * aggregate the body addresses; the pair return goes back out
     * through the annotated hidden pointer. */
    T_ASSERT(t, run_abi(&f, "struct M { double d; long l; };\n"
                            "struct M through(struct M m) { return m; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(atxt(&f), "func void @through(ptr %0, f64 %1, i64 %2) "
                                 "abi(pair_si)") != NULL);
    /* prologue: 16-byte reassembly slot, both eightbytes stored */
    T_ASSERT(t, strstr(atxt(&f), "alloca 16, align 8") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "store f64 %1,") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "store i64 %2,") != NULL);
    abi_free(&f);
}

void test_abi_byval_single_copy(TestCtx *t)
{
    AbiFix f;

    /* DoD: the MEMORY-class copy happens ONCE — one alloca, one memcpy,
     * the annotated pointer. */
    T_ASSERT(t, run_abi(&f, "struct B { int a[9]; };\n"
                            "struct B g;\n"
                            "void take(struct B b);\n"
                            "void f(void) { take(g); }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, acount(atxt(&f), "memcpy"), 1);
    T_ASSERT_EQ_INT(t, acount(atxt(&f), "alloca"), 1);
    T_ASSERT_EQ_INT(t, acount(atxt(&f), "byval(36)"), 1);
    abi_free(&f);
}

void test_abi_va_start_constants(TestCtx *t)
{
    AbiFix f;

    /* (2 gp, 1 fp) named: gp_offset = 16, fp_offset = 48 + 16 = 64. */
    T_ASSERT(t, run_abi(&f, "typedef __builtin_va_list va_list;\n"
                            "void v(int a, double d, int b, ...) {\n"
                            "  va_list ap;\n"
                            "  __builtin_va_start(ap, b);\n"
                            "  __builtin_va_end(ap);\n"
                            "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(atxt(&f), "store i32 16,") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "store i32 64,") != NULL);
    T_ASSERT(t, strstr(atxt(&f), "va_start %") != NULL);
    abi_free(&f);

    /* (0, 0) named -> 0 and 48; six gp named -> gp_offset 48. */
    T_ASSERT(t, run_abi(&f, "typedef __builtin_va_list va_list;\n"
                            "void w(int a, int b, int c, int d, int e,"
                            " int g, ...) {\n"
                            "  va_list ap;\n"
                            "  __builtin_va_start(ap, g);\n"
                            "  __builtin_va_end(ap);\n"
                            "}\n"));
    T_ASSERT(t, strstr(atxt(&f), "store i32 48,") != NULL);
    abi_free(&f);
}

void test_abi_va_arg_shapes(TestCtx *t)
{
    AbiFix f;

    /* int: gp diamond (limit 40 printed as ule 40); double: fp diamond
     * (ule 160); long double and a MEMORY struct: straight-line overflow
     * (no branch — count the diamonds). */
    T_ASSERT(t, run_abi(&f, "typedef __builtin_va_list va_list;\n"
                            "struct B { char c[17]; };\n"
                            "int f(int n, ...) {\n"
                            "  va_list ap;\n"
                            "  int i; double d; long double ld;\n"
                            "  struct B b;\n"
                            "  __builtin_va_start(ap, n);\n"
                            "  i = __builtin_va_arg(ap, int);\n"
                            "  d = (double)__builtin_va_arg(ap, double);\n"
                            "  ld = __builtin_va_arg(ap, long double);\n"
                            "  b = __builtin_va_arg(ap, struct B);\n"
                            "  __builtin_va_end(ap);\n"
                            "  return i + (int)d + (int)ld + b.c[0];\n"
                            "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(atxt(&f), "icmp ule i32 %") != NULL);
    T_ASSERT(t, strstr(atxt(&f), ", 40") != NULL);  /* gp limit, n=1 */
    T_ASSERT(t, strstr(atxt(&f), ", 160") != NULL); /* fp limit, n=1 */
    /* exactly TWO diamonds: ld and the struct take the straight line */
    T_ASSERT_EQ_INT(t, acount(atxt(&f), "va.reg"), 2 * 2);
    /* the 16-aligned long double bumps the overflow cursor via mask */
    T_ASSERT(t, strstr(atxt(&f), ", -16") != NULL);
    abi_free(&f);
}

void test_abi_va_list_both_forms_identical(TestCtx *t)
{
    AbiFix f;

    /* DoD 3: passing `ap` (decayed) and `&ap` resolve to the same record
     * address, so the two helpers lower to IDENTICAL bodies (compare the
     * printed text of both, modulo the function names). */
    T_ASSERT(
        t,
        run_abi(&f,
                "typedef __builtin_va_list va_list;\n"
                "int use1(va_list ap) { return __builtin_va_arg(ap, int); }\n"
                "int use2(va_list *ap) { return __builtin_va_arg(*ap, int);"
                " }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    {
        const char *b1 = strstr(atxt(&f), "func i32 @use1(ptr %0) {");
        const char *b2 = strstr(atxt(&f), "func i32 @use2(ptr %0) {");
        const char *e1 = b1 ? strstr(b1, "\n}\n") : NULL;
        const char *e2 = b2 ? strstr(b2, "\n}\n") : NULL;

        T_ASSERT(t, b1 && b2 && e1 && e2);
        if (b1 && b2 && e1 && e2) {
            size_t l1 = (size_t)(e1 - b1) - strlen("func i32 @use1(ptr %0) {");
            size_t l2 = (size_t)(e2 - b2) - strlen("func i32 @use2(ptr %0) {");
            const char *body1 = b1 + strlen("func i32 @use1(ptr %0) {");
            const char *body2 = b2 + strlen("func i32 @use2(ptr %0) {");

            T_ASSERT(t, l1 == l2 && memcmp(body1, body2, l1) == 0);
        }
    }
    abi_free(&f);
}

void test_abi_string_pool_two_runs_identical(TestCtx *t)
{
    AbiFix f1, f2;
    static const char src[] =
        "const char *a(void) { return \"one\"; }\n"
        "const char *b(void) { return \"two\"; }\n"
        "const char *c(void) { return \"one\"; }\n"
        "int big(void) { int t[12] = {1,2,3,4,5,6,7,8,9,10,11,12};"
        " return t[0]; }\n";

    /* DoD 4: emission is byte-stable across two INDEPENDENT runs. */
    T_ASSERT(t, run_abi(&f1, src));
    T_ASSERT(t, run_abi(&f2, src));
    T_ASSERT(t, f1.text.len == f2.text.len &&
                    memcmp(f1.text.data, f2.text.data, f1.text.len) == 0);
    abi_free(&f1);
    abi_free(&f2);
}
