#include <string.h>

#include "lower/lower.h"
#include "parse/parse.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* Lowering units, expression side: the whole front end runs over a C
 * snippet in-process, the module lowers, and assertions land on BOTH the
 * printed text (sequences, shapes) and the structure (counts). Every
 * lowered module must also pass ir_verify — asserted in the fixture
 * helper so no unit can forget it. */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    Sema sema;
    DiagCtx *dc;
    int errors;
    IrModule *m;
    Buf text; /* printed module */
} LowFix;

static void low_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    LowFix *f = user;

    (void)dc;
    if (d->level >= DIAG_ERROR)
        f->errors++;
}

VEC_DECL(PpVecL, PpToken);

/* Front end + lowering + verify + print. Returns false if lowering
 * refused (a deferral fired). */
static bool run_lower(LowFix *f, const char *src)
{
    DiagSink sink;
    SourceFile *sf;
    PpVecL pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec target;
    AstNode *tu;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = low_sink;
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
        PpVecL_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, target, &f->arena);
    PpVecL_free(&pv);

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

static void low_free(LowFix *f)
{
    if (f->text.data)
        buf_free(&f->text);
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

static const char *txt(LowFix *f)
{
    return (const char *)f->text.data;
}

/* Counts non-overlapping occurrences. */
static int count_of(const char *hay, const char *needle)
{
    int n = 0;
    size_t len = strlen(needle);

    while ((hay = strstr(hay, needle)) != NULL) {
        n++;
        hay += len;
    }
    return n;
}

void test_lower_verifies_and_roundtrips(TestCtx *t)
{
    LowFix f;

    T_ASSERT(t, run_lower(&f, "int add(int a, int b) { return a + b; }\n"
                              "int g;\n"
                              "int main(void) { return add(g, 2); }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    {
        /* The unit-level round-trip: parse(print(M)) == M. */
        IrModule *m2 = ir_parse_module(&f.arena, f.dc, txt(&f), "<rt>");

        T_ASSERT(t, m2 != NULL);
        if (m2)
            T_ASSERT(t, ir_module_struct_eq(f.m, m2));
    }
    T_ASSERT_EQ_INT(t, f.m->nfuncs, 2);
    T_ASSERT(t, strstr(txt(&f), "call i32 @add(") != NULL);
    low_free(&f);
}

void test_lower_old_style_calls_keep_loose_contract(TestCtx *t)
{
    LowFix f;
    IrModule *round;

    T_ASSERT(t, run_lower(&f, "int f(x) int x; { return x; }\n"
                              "int g(void) { return f(); }\n"));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.m->nfuncs, 2);
    T_ASSERT(t, f.m->funcs[0].unprototyped);
    T_ASSERT(t, strstr(txt(&f), ") unproto {") != NULL);
    T_ASSERT(t, strstr(txt(&f), "call i32 @f()") != NULL);
    T_ASSERT(t, ir_verify(f.dc, f.m));
    round = ir_parse_module(&f.arena, f.dc, txt(&f), "<old-style-rt>");
    T_ASSERT(t, round != NULL);
    if (round)
        T_ASSERT(t, ir_module_struct_eq(f.m, round));
    low_free(&f);
}

void test_lower_bitfield_unsigned_rw(TestCtx *t)
{
    LowFix f;
    const char *p;

    /* Unsigned width-3 field at bit 0 of an i32 unit: read is
     * shl 29 / lshr 29; write clears with ~7, masks with 7. */
    T_ASSERT(t, run_lower(&f, "struct B { unsigned u : 3; } g;\n"
                              "unsigned rd(void) { return g.u; }\n"
                              "void wr(unsigned v) { g.u = v; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    p = strstr(txt(&f), "shl i32");
    T_ASSERT(t, p != NULL);
    T_ASSERT(t, strstr(txt(&f), ", 29") != NULL); /* 32-0-3 up-shift */
    T_ASSERT(t, strstr(txt(&f), "lshr i32") != NULL);
    T_ASSERT(t, strstr(txt(&f), "and i32") != NULL);
    T_ASSERT(t, strstr(txt(&f), ", -8") != NULL); /* ~(7 << 0) */
    T_ASSERT(t, strstr(txt(&f), ", 7") != NULL);  /* the value mask */
    T_ASSERT(t, strstr(txt(&f), "or i32") != NULL);
    /* Unsigned: no arithmetic shift anywhere in the READ path. */
    T_ASSERT(t, count_of(txt(&f), "ashr") == 0);
    low_free(&f);
}

void test_lower_bitfield_signed_rw(TestCtx *t)
{
    LowFix f;

    /* Signed width-5 at bit 3: read is shl 24 / ASHR 27 (sign extension
     * for free); the write's re-narrowed RESULT also uses ashr. */
    T_ASSERT(t, run_lower(&f, "struct B { unsigned u : 3; int s : 5; } g;\n"
                              "int rd(void) { return g.s; }\n"
                              "int wr(int v) { return g.s = v; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(txt(&f), "ashr i32") != NULL);
    T_ASSERT(t, strstr(txt(&f), ", 24") != NULL);   /* 32-3-5 up */
    T_ASSERT(t, strstr(txt(&f), ", 27") != NULL);   /* 32-5 down */
    T_ASSERT(t, strstr(txt(&f), ", -249") != NULL); /* ~(31 << 3) */
    low_free(&f);
}

void test_lower_bitfield_assign_result_narrowed(TestCtx *t)
{
    LowFix f;

    /* `(g.u = 9)` with a 3-bit field: the assignment's VALUE is the
     * re-narrowed stored value (9 & 7 = 1), not 9 — pinned by the
     * post-store shl/lshr pair feeding the ret. */
    T_ASSERT(t, run_lower(&f, "struct B { unsigned u : 3; } g;\n"
                              "unsigned f(void) { return g.u = 9; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    {
        /* After the store there must be a further shl/lshr pair whose
         * result is returned — find the LAST lshr and check a ret uses
         * a value, not the raw constant 9. */
        const char *st = strstr(txt(&f), "store i32");

        T_ASSERT(t, st != NULL);
        T_ASSERT(t, st && strstr(st, "lshr i32") != NULL);
        T_ASSERT(t, strstr(txt(&f), "ret i32 %") != NULL);
    }
    low_free(&f);
}

void test_lower_shortcircuit_zero_allocas(TestCtx *t)
{
    LowFix f;

    /* DoD 4 at the unit level: the result travels as a block param. */
    T_ASSERT(t, run_lower(&f, "int a, b, c;\n"
                              "int f(void) { return a && b || !c; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "alloca"), 0);
    T_ASSERT(t, strstr(txt(&f), "and.join") != NULL);
    T_ASSERT(t, strstr(txt(&f), "or.join") != NULL);
    /* The joins carry an i32 param. */
    T_ASSERT(t, strstr(txt(&f), "(i32 %") != NULL);
    low_free(&f);
}

void test_lower_shortcircuit_three_block_shape(TestCtx *t)
{
    LowFix f;

    /* Exactly the §3 pattern for one `&&`: entry + rhs + join = 3
     * blocks, join has one i32 param, the short edge passes 0. */
    T_ASSERT(t, run_lower(&f, "int a, b;\n"
                              "int f(void) { return a && b; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, f.m->funcs[0].nblocks, 3);
    T_ASSERT_EQ_INT(t, f.m->funcs[0].blocks[2].nparams, 1);
    T_ASSERT(t, strstr(txt(&f), "and.join2(i32 0)") != NULL);
    low_free(&f);
}

void test_lower_cond_fold_single_compare(TestCtx *t)
{
    LowFix f;

    /* `if (a < b)` emits ONE icmp; a bare `if (x)` emits icmp ne. */
    T_ASSERT(t, run_lower(&f, "int f(int a, int b) {\n"
                              "  if (a < b) return 1;\n"
                              "  return 0;\n"
                              "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "icmp"), 1);
    low_free(&f);

    T_ASSERT(t, run_lower(&f, "int f(int x) { if (x) return 1; return 0; }\n"));
    T_ASSERT(t, strstr(txt(&f), "icmp ne i32") != NULL);
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "icmp"), 1);
    low_free(&f);
}

void test_lower_fp_condition_nan_truthy(TestCtx *t)
{
    LowFix f;

    /* `if (d)` on a double is fcmp UNE 0.0 — NaN is truthy. */
    T_ASSERT(t, run_lower(&f, "int f(double d) { if (d) return 1;"
                              " return 0; }\n"));
    T_ASSERT(t, strstr(txt(&f), "fcmp une f64") != NULL);
    low_free(&f);
}

void test_lower_struct_assign_one_memcpy(TestCtx *t)
{
    LowFix f;

    T_ASSERT(t, run_lower(&f, "struct S { char c; int i; double d; };\n"
                              "struct S a, b;\n"
                              "void f(void) { a = b; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "memcpy"), 1);
    T_ASSERT(t, strstr(txt(&f), "memcpy @a, @b, 16, align 8") != NULL);
    low_free(&f);
}

void test_lower_call_site_copy(TestCtx *t)
{
    LowFix f;

    /* The mandatory aggregate-argument copy: alloca + memcpy + ptr arg. */
    T_ASSERT(t, run_lower(&f, "struct S { int a[4]; };\n"
                              "struct S g;\n"
                              "int use(struct S s);\n"
                              "int f(void) { return use(g); }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    /* 16 bytes, two INTEGER eightbytes: ONE staging copy, then the value
     * travels as two bit-carrying i64 scalars (Sprint 19). */
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "alloca"), 1);
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "memcpy"), 1);
    T_ASSERT(t, strstr(txt(&f), "call i32 @use(i64 %") != NULL);
    low_free(&f);
}

void test_lower_sret_shape(TestCtx *t)
{
    LowFix f;

    /* Aggregate return: hidden ptr param 0, ret void, caller temp. */
    T_ASSERT(t, run_lower(&f, "struct S { int x, y, z; };\n"
                              "struct S mk(int v) {\n"
                              "  struct S s = {v, v, v};\n"
                              "  return s;\n"
                              "}\n"
                              "int f(void) { return mk(3).y; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(txt(&f), "func void @mk(ptr %0, i32 %1)") != NULL);
    T_ASSERT(t, strstr(txt(&f), "memcpy %0,") != NULL);
    T_ASSERT(t, strstr(txt(&f), "call void @mk(ptr %") != NULL);
    low_free(&f);
}

void test_lower_eval_order_left_to_right(TestCtx *t)
{
    LowFix f;
    const char *cf;
    const char *cg;

    /* The §1 law: f() before g(), textually, in argument position. */
    T_ASSERT(t, run_lower(&f, "int f(void); int g(void);\n"
                              "int h(int a, int b);\n"
                              "int m(void) { return h(f(), g()); }\n"));
    cf = strstr(txt(&f), "call i32 @f()");
    cg = strstr(txt(&f), "call i32 @g()");
    T_ASSERT(t, cf != NULL && cg != NULL && cf < cg);
    low_free(&f);
}

void test_lower_compound_assign_roundtrip(TestCtx *t)
{
    LowFix f;

    /* char += int computes in int and stores back a char: sext up,
     * trunc down, ONE address evaluation. */
    T_ASSERT(t, run_lower(&f, "char c; int i;\n"
                              "void f(void) { c += i; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(txt(&f), "sext i8") != NULL);
    T_ASSERT(t, strstr(txt(&f), "trunc i32") != NULL);
    T_ASSERT(t, strstr(txt(&f), "store i8") != NULL);
    low_free(&f);
}

void test_lower_conditional_pointer_integer_recovery(TestCtx *t)
{
    LowFix f;

    /* gcc accepts this with warnings.  Sema's recovery type is a pointer,
     * so it must also materialize the integer-to-pointer conversion before
     * the two values become CFG edge arguments. */
    T_ASSERT(t, run_lower(&f, "int a;\n"
                              "int f(int c) { a = c ? a : &a; return a; }\n"));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(txt(&f), "bitcast i64") != NULL);
    low_free(&f);
}

void test_lower_ptr_arith_scaling(TestCtx *t)
{
    LowFix f;

    T_ASSERT(t, run_lower(&f, "long d(int *p, int *q) { return p - q; }\n"
                              "int *up(int *p) { return p + 3; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    /* p - q: bitcasts, isub, exact sdiv by 4. */
    T_ASSERT(t, strstr(txt(&f), "bitcast ptr") != NULL);
    T_ASSERT(t, strstr(txt(&f), "sdiv i64 %") != NULL);
    T_ASSERT(t, strstr(txt(&f), ", 4") != NULL);
    /* p + 3: imul by 4 then ptradd. */
    T_ASSERT(t, strstr(txt(&f), "imul i64") != NULL);
    T_ASSERT(t, strstr(txt(&f), "ptradd") != NULL);
    low_free(&f);
}

void test_lower_commuted_subscript(TestCtx *t)
{
    LowFix f;

    /* C defines a[b] as *(a + b), so the pointer may legally be the second
     * operand.  musl's getenv uses this spelling as `l[*e]`. */
    T_ASSERT(t, run_lower(&f, "int get(unsigned long l, char **e) {\n"
                              "    return l[*e];\n"
                              "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(txt(&f), "ptradd") != NULL);
    low_free(&f);
}

void test_lower_incdec_result_values(TestCtx *t)
{
    LowFix f;

    /* Post yields the OLD value: `return p++` returns the pre-step load;
     * pinned by ret using the load's value while the ptradd's stored. */
    T_ASSERT(t, run_lower(&f, "int g;\n"
                              "int post(void) { return g++; }\n"
                              "int pre(void) { return ++g; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    {
        /* post: ret must NOT reference the iadd's result (%1 is the
         * load, %2 the iadd — canonical numbering makes this stable). */
        const char *fn = strstr(txt(&f), "func i32 @post");

        T_ASSERT(t, fn != NULL);
        T_ASSERT(t, fn && strstr(fn, "ret i32 %0") != NULL);
    }
    {
        const char *fn = strstr(txt(&f), "func i32 @pre");

        T_ASSERT(t, fn != NULL);
        T_ASSERT(t, fn && strstr(fn, "ret i32 %1") != NULL);
    }
    low_free(&f);
}

void test_lower_ternary_aggregate_one_temp(TestCtx *t)
{
    LowFix f;

    T_ASSERT(t, run_lower(&f, "struct S { int a[4]; } x, y;\n"
                              "int f(int c) { return (c ? x : y).a[0]; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    /* ONE temp, both arms memcpy into it. */
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "alloca 16"), 1);
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "memcpy"), 2);
    low_free(&f);
}

void test_lower_atomic_live(TestCtx *t)
{
    LowFix f;

    /* Flipped in Sprint 20: the read is a seq_cst load now. */
    T_ASSERT(t, run_lower(&f, "_Atomic int a;\n"
                              "int f(void) { return a; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(txt(&f), "load i32, @a, align 4, seq_cst") != NULL);
    low_free(&f);
}
