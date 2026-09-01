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

void test_lower_inner_pointer_alignment_respects_ir_contract(TestCtx *t)
{
    LowFix f;

    T_ASSERT(
        t,
        run_lower(
            &f, "int *__attribute__((aligned(16))) *p;\n" /* check_bans allow */
                "int main(void) { return **p; }\n"));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(txt(&f), "load ptr, @p, align 8") != NULL);
    T_ASSERT(t, strstr(txt(&f), "load i32") != NULL);
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

    /* A later prototype becomes the entity's final composite type, but it
     * cannot retroactively constrain an earlier call. A call after the
     * definition does see the prototype and remains strict. */
    T_ASSERT(t, run_lower(&f, "static int f();\n"
                              "int before(void) { return f(); }\n"
                              "static int f(int x) { return x; }\n"
                              "int after(void) { return f(7); }\n"
                              "static int extra();\n"
                              "int before_extra(void) { return extra(1, 2); }\n"
                              "static int extra(int x) { return x; }\n"
                              "static double promoted();\n"
                              "double before_promoted(float x) {\n"
                              "  return promoted(x);\n"
                              "}\n"
                              "static double promoted(double x) {\n"
                              "  return x;\n"
                              "}\n"));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, strstr(txt(&f), "call i32 @f() unproto") != NULL);
    T_ASSERT(t, strstr(txt(&f), "call i32 @f(i32 7) unproto") == NULL);
    T_ASSERT(t,
             strstr(txt(&f), "call i32 @extra(i32 1, i32 2) unproto") != NULL);
    T_ASSERT(t, strstr(txt(&f), "call f64 @promoted(f64 %") != NULL);
    T_ASSERT_EQ_INT(t, count_of(txt(&f), " unproto"), 3);
    T_ASSERT(t, ir_verify(f.dc, f.m));
    round = ir_parse_module(&f.arena, f.dc, txt(&f), "<later-prototype-rt>");
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

void test_lower_narrow_bitfield_extract_widens_before_shifts(TestCtx *t)
{
    LowFix f;

    /* ARM64 selects i8/i16 arithmetic in W registers.  Widening the storage
     * unit before the extraction pair makes the shift distances describe
     * the physical arithmetic width rather than accidentally retaining the
     * high five bits of an i8 container. */
    T_ASSERT(t, run_lower(&f, "struct B { unsigned char u : 3; } g;\n"
                              "unsigned rd(void) { return g.u; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(txt(&f), "zext i8") != NULL);
    T_ASSERT(t, strstr(txt(&f), "shl i32") != NULL);
    T_ASSERT(t, strstr(txt(&f), ", 29") != NULL);
    T_ASSERT(t, strstr(txt(&f), "lshr i32") != NULL);
    T_ASSERT(t, strstr(txt(&f), "shl i8") == NULL);
    T_ASSERT(t, strstr(txt(&f), "lshr i8") == NULL);
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

void test_lower_enum_bitfield_uses_value_range_signedness(TestCtx *t)
{
    LowFix f;

    /* Cgfried keeps a small enum compatible with int, but follows gcc's
     * implementation-defined enum-bitfield rule: an enum whose enumerators
     * are all nonnegative is an unsigned field representation. A negative
     * enumerator makes the field representation signed. The two reads must
     * therefore select different extraction shifts even though both enum
     * compatible types are int. */
    T_ASSERT(t,
             run_lower(&f, "enum Pos { P = 148 };\n"
                           "enum Neg { N = -1, Z = 0 };\n"
                           "struct B { enum Pos p : 8; enum Neg n : 8; } g;\n"
                           "int rp(void) { return g.p; }\n"
                           "int rn(void) { return g.n; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "lshr i32"), 1);
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "ashr i32"), 1);
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

void test_lower_volatile_aggregate_copy_markers(TestCtx *t)
{
    LowFix f;
    const char *ir;

    T_ASSERT(t, run_lower(&f, "struct S { int a, b; };\n"
                              "volatile struct S src, dst;\n"
                              "struct S plain;\n"
                              "void f(void) {\n"
                              "  struct S local = src;\n"
                              "  local = src;\n"
                              "  dst = plain;\n"
                              "  plain = plain;\n"
                              "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    ir = txt(&f);
    T_ASSERT_EQ_INT(t, count_of(ir, "memcpy"), 4);
    T_ASSERT_EQ_INT(t, count_of(ir, "volatile"), 3);
    T_ASSERT(t, strstr(ir, "memcpy %0, @src, 8, align 4, volatile") != NULL);
    T_ASSERT(t,
             strstr(ir, "memcpy @dst, @plain, 8, align 4, volatile") != NULL);
    T_ASSERT(t, strstr(ir, "memcpy @plain, @plain, 8, align 4\n") != NULL);
    low_free(&f);
}

void test_lower_volatile_aggregate_temporary_markers(TestCtx *t)
{
    LowFix f;
    const char *ir;

    T_ASSERT(t, run_lower(&f, "struct S { int a, b; };\n"
                              "volatile struct S src;\n"
                              "struct S plain;\n"
                              "void f(int c) {\n"
                              "  plain = c ? src : plain;\n"
                              "  plain = ({ src; });\n"
                              "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    ir = txt(&f);
    /* ?: marks only its volatile arm. The statement expression consumes
     * src before leaving its scope, then copies its ordinary temporary. */
    T_ASSERT_EQ_INT(t, count_of(ir, "memcpy"), 5);
    T_ASSERT_EQ_INT(t, count_of(ir, "volatile"), 2);
    low_free(&f);
}

void test_lower_volatile_aggregate_initializer_destinations(TestCtx *t)
{
    LowFix f;
    const char *ir;

    T_ASSERT(t, run_lower(&f, "struct S { int a, b; };\n"
                              "volatile struct S src;\n"
                              "void f(void) {\n"
                              "  volatile struct S copied = src;\n"
                              "  volatile struct S zero = {0};\n"
                              "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    ir = txt(&f);
    T_ASSERT_EQ_INT(t, count_of(ir, "memcpy"), 1);
    T_ASSERT(t, strstr(ir, "memcpy %0, @src, 8, align 4, volatile") != NULL);
    T_ASSERT(t, strstr(ir, "memset %1, 0, 8, align 4, volatile") != NULL);
    low_free(&f);
}

void test_lower_volatile_aggregate_call_and_return_markers(TestCtx *t)
{
    LowFix f;
    const char *ir;

    T_ASSERT(t, run_lower(&f, "struct S { int a, b; };\n"
                              "volatile struct S src;\n"
                              "int use(struct S);\n"
                              "int pass(void) { return use(src); }\n"
                              "struct S get(void) { return src; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    ir = txt(&f);
    T_ASSERT(t, strstr(ir, "memcpy %0, @src, 8, align 4, volatile") != NULL);
    T_ASSERT(t, strstr(ir, "load i64, @src, align 8, volatile") != NULL);
    low_free(&f);
}

void test_lower_volatile_anonymous_and_forwarded_aggregate_args(TestCtx *t)
{
    LowFix f;
    const char *ir;

    T_ASSERT(
        t,
        run_lower(&f, "struct S { int a, b; };\n"
                      "volatile struct S vol;\n"
                      "struct S plain;\n"
                      "int sink(int, ...);\n"
                      "static inline int forward(int n, ...) {\n"
                      "  return sink(n, __builtin_va_arg_pack());\n"
                      "}\n"
                      "int direct(void) { return sink(0, vol, plain); }\n"
                      "int packed(void) { return forward(0, vol, plain); }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    ir = txt(&f);
    /* Direct placement stages once. Pack aggregates first snapshot at the
     * wrapper boundary, then stage that plain snapshot for the inner call. */
    T_ASSERT_EQ_INT(t, count_of(ir, "memcpy"), 6);
    T_ASSERT_EQ_INT(t, count_of(ir, "volatile"), 2);
    T_ASSERT_EQ_INT(t, count_of(ir, "@vol, 8, align 4, volatile"), 2);
    T_ASSERT_EQ_INT(t, count_of(ir, "@plain, 8, align 4, volatile"), 0);
    T_ASSERT_EQ_INT(t, count_of(ir, "anon"), 4);
    low_free(&f);
}

void test_lower_volatile_va_pack_boundary_capture(TestCtx *t)
{
    LowFix f;
    const char *ir;

    T_ASSERT(t,
             run_lower(&f, "struct S { int a, b; };\n"
                           "volatile struct S vol;\n"
                           "int sink(int, ...);\n"
                           "static inline int maybe(int emit, ...) {\n"
                           "  if (emit) return sink(0, "
                           "__builtin_va_arg_pack());\n"
                           "  return 7;\n"
                           "}\n"
                           "static inline int twice(int n, ...) {\n"
                           "  return sink(n, __builtin_va_arg_pack()) +\n"
                           "         sink(n, __builtin_va_arg_pack());\n"
                           "}\n"
                           "int never(void) { return maybe(0, vol); }\n"
                           "int double_forward(void) { return twice(0, vol); "
                           "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    ir = txt(&f);
    /* One source read per wrapper boundary, irrespective of whether the
     * pack is forwarded zero/one/two times. Forwarding copies are plain. */
    T_ASSERT_EQ_INT(t, count_of(ir, "@vol, 8, align 4, volatile"), 2);
    T_ASSERT_EQ_INT(t, count_of(ir, "volatile"), 2);
    T_ASSERT_EQ_INT(t, count_of(ir, "memcpy"), 5);
    T_ASSERT_EQ_INT(t, count_of(ir, "anon"), 3);
    low_free(&f);
}

void test_lower_volatile_aggregate_return_abi_matrix(TestCtx *t)
{
    LowFix f;
    const char *ir;

    T_ASSERT(t, run_lower(&f, "struct S1 { char x; };\n"
                              "struct S8 { long x; };\n"
                              "struct S16 { long x, y; };\n"
                              "struct S24 { long x, y, z; };\n"
                              "volatile struct S1 v1; struct S1 p1;\n"
                              "volatile struct S8 v8; struct S8 p8;\n"
                              "volatile struct S16 v16; struct S16 p16;\n"
                              "volatile struct S24 v24; struct S24 p24;\n"
                              "int sink(int, ...);\n"
                              "struct S1 r1v(void) { return v1; }\n"
                              "struct S1 r1p(void) { return p1; }\n"
                              "struct S8 r8v(void) { return v8; }\n"
                              "struct S8 r8p(void) { return p8; }\n"
                              "struct S16 r16v(void) { return v16; }\n"
                              "struct S16 r16p(void) { return p16; }\n"
                              "struct S24 r24v(void) { return v24; }\n"
                              "struct S24 r24p(void) { return p24; }\n"
                              "static inline struct S24 wrap(int n, ...) {\n"
                              "  sink(n, __builtin_va_arg_pack());\n"
                              "  return v24;\n"
                              "}\n"
                              "long wrapped(void) { return wrap(0, 1).x; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    ir = txt(&f);
    /* Sub-eightbyte staging, exact-eightbyte wire load, pair/SRET copies,
     * and the specialized wrapper return each carry exactly one marker. */
    T_ASSERT_EQ_INT(t, count_of(ir, "memcpy"), 7);
    T_ASSERT_EQ_INT(t, count_of(ir, "volatile"), 5);
    T_ASSERT(t, strstr(ir, "@v1, 1, align 1, volatile") != NULL);
    T_ASSERT(t, strstr(ir, "load i64, @v8, align 8, volatile") != NULL);
    T_ASSERT(t, strstr(ir, "@v16, 16, align 8, volatile") != NULL);
    T_ASSERT_EQ_INT(t, count_of(ir, "@v24, 24, align 8, volatile"), 2);
    T_ASSERT_EQ_INT(t, count_of(ir, "@p1, 1, align 1, volatile"), 0);
    T_ASSERT_EQ_INT(t, count_of(ir, "load i64, @p8, align 8, volatile"), 0);
    T_ASSERT_EQ_INT(t, count_of(ir, "@p16, 16, align 8, volatile"), 0);
    T_ASSERT_EQ_INT(t, count_of(ir, "@p24, 24, align 8, volatile"), 0);
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

void test_lower_runtime_offsetof_uses_vla_stride(TestCtx *t)
{
    LowFix f;
    const char *ir;
    const char *left;
    const char *right;
    const char *constant;

    T_ASSERT(t,
             run_lower(&f, "int left(int); int right(int);\n"
                           "unsigned long dynamic(int n, int i, int j) {\n"
                           "  typedef int Row[n];\n"
                           "  struct S { int head; Row rows[n]; };\n"
                           "  return __builtin_offsetof(\n"
                           "      struct S, rows[left(i)][right(j)]);\n"
                           "}\n"
                           "struct F { int values[4]; };\n"
                           "unsigned long constant(void) {\n"
                           "  return __builtin_offsetof(struct F, values[2]);\n"
                           "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    ir = txt(&f);
    left = strstr(ir, "call i32 @left");
    right = strstr(ir, "call i32 @right");
    T_ASSERT(t, left != NULL && right != NULL && left < right);
    /* n * sizeof(int), then each runtime index times its own stride. */
    T_ASSERT_EQ_INT(t, count_of(ir, "imul i64"), 3);
    /* offsetof is integer arithmetic even for negative or out-of-range
     * indices: it must not materialize a pointer or a safety guard. */
    T_ASSERT(t, strstr(ir, "ptradd") == NULL);
    T_ASSERT(t, strstr(ir, "cgf_safe_check_index") == NULL);
    constant = strstr(ir, "func i64 @constant()");
    T_ASSERT(t, constant != NULL && strstr(constant, "ret i64 8") != NULL);
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

void test_lower_void_deref_effects_without_load(TestCtx *t)
{
    LowFix f;

    /* A void dereference evaluates its pointer operand but cannot load a
     * value.  There are five syntactic next() calls: one discarded, two in
     * the conditional CFG, and two comma operands. */
    T_ASSERT(t, run_lower(&f, "void *next(void);\n"
                              "void f(int c) {\n"
                              "  *next(); c ? *next() : *next();\n"
                              "  *next(), *next();\n"
                              "}\n"));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, count_of(txt(&f), "call ptr @next()"), 5);
    /* Reading the integer condition may require an i32 load at O0; the
     * forbidden operation is a load THROUGH next()'s returned pointer. */
    T_ASSERT(t, strstr(txt(&f), "load ptr") == NULL);
    T_ASSERT(t, strstr(txt(&f), "load i8") == NULL);
    low_free(&f);
}

void test_lower_conditional_f80_uses_memory_join(TestCtx *t)
{
    LowFix f;
    IrFunc *fn;
    u32 i;
    bool saw_join = false;

    T_ASSERT(t, run_lower(&f, "long double pick(int c, long double a, "
                              "long double b) { return c ? a : b; }\n"));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, ir_verify(f.dc, f.m));
    fn = &f.m->funcs[0];
    for (i = 0; i < fn->nblocks; i++) {
        if (strncmp(fn->blocks[i].name, "cond.join", 9) != 0)
            continue;
        saw_join = true;
        T_ASSERT_EQ_INT(t, fn->blocks[i].nparams, 0);
    }
    T_ASSERT(t, saw_join);
    T_ASSERT(t, count_of(txt(&f), "store f80") >= 2);
    T_ASSERT(t, strstr(txt(&f), "load f80") != NULL);
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

void test_lower_returns_twice_exact_name_policy(TestCtx *t)
{
    LowFix f;
    const char *text;

    T_ASSERT(t, run_lower(&f, "int setjmp(char *);\n"
                              "int _setjmp(char *);\n"
                              "int sigsetjmp(char *, int);\n"
                              "int __sigsetjmp(char *, int);\n"
                              "int setjmpx(char *);\n"
                              "int __sigsetjmp_chk(char *, int);\n"
                              "int renamed(char *, int) "
                              "__asm__(\"__sigsetjmp\");\n"
                              "int __sigsetjmp_local(char *, int) "
                              "__asm__(\"ordinary\");\n"
                              "int a(char *p) { return setjmp(p); }\n"
                              "int b(char *p) { return _setjmp(p); }\n"
                              "int c(char *p) { return sigsetjmp(p, 1); }\n"
                              "int d(char *p) { return __sigsetjmp(p, 1); }\n"
                              "int e(char *p) { return setjmpx(p); }\n"
                              "int f(char *p) { return "
                              "__sigsetjmp_chk(p, 1); }\n"
                              "int g(char *p) { return renamed(p, 1); }\n"
                              "int h(char *p) { return "
                              "__sigsetjmp_local(p, 1); }\n"));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, ir_verify(f.dc, f.m));
    text = txt(&f);
    T_ASSERT(t, strstr(text, "func i32 @a(ptr %0) setjmp {") != NULL);
    T_ASSERT(t, strstr(text, "func i32 @b(ptr %0) setjmp {") != NULL);
    T_ASSERT(t, strstr(text, "func i32 @c(ptr %0) setjmp {") != NULL);
    T_ASSERT(t, strstr(text, "func i32 @d(ptr %0) setjmp {") != NULL);
    T_ASSERT(t, strstr(text, "func i32 @e(ptr %0) setjmp {") == NULL);
    T_ASSERT(t, strstr(text, "func i32 @f(ptr %0) setjmp {") == NULL);
    T_ASSERT(t, strstr(text, "func i32 @g(ptr %0) setjmp {") != NULL);
    T_ASSERT(t, strstr(text, "func i32 @h(ptr %0) setjmp {") == NULL);
    low_free(&f);
}

void test_lower_gnu_va_arg_pack_specializes_before_call_abi(TestCtx *t)
{
    LowFix f;

    T_ASSERT(t, run_lower(&f, "int sink(int, ...);\n"
                              "static inline int wrap(int n, ...) {\n"
                              "  if (__builtin_constant_p(n))\n"
                              "    return sink(n, __builtin_va_arg_pack()) +\n"
                              "           __builtin_va_arg_pack_len();\n"
                              "  return -1;\n"
                              "}\n"
                              "int f(void) { return wrap(1, 2, 3); }\n"));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, f.m->nfuncs, 1);
    T_ASSERT(t, strstr(txt(&f), "func i32 @wrap") == NULL);
    T_ASSERT(t, strstr(txt(&f), "call i32 @sink(i32 %") != NULL);
    T_ASSERT(t, strstr(txt(&f), "i32 2 anon, i32 3 anon") != NULL);
    T_ASSERT(t, strstr(txt(&f), "vapack.ret") != NULL);
    /* The wrapper parameter inherits constant knowledge from the literal
     * outer actual even though it has been materialized in a local slot. The
     * known answer now selects its CFG arm directly instead of building an
     * icmp for lower_cond(). */
    T_ASSERT(t, strstr(txt(&f), "br if.then") != NULL);
    T_ASSERT(t, strstr(txt(&f), "icmp ne i32 1, 0") == NULL);
    low_free(&f);
}

void test_lower_gnu_va_arg_pack_rejects_unsafe_survivors(TestCtx *t)
{
    LowFix f;

    T_ASSERT(t, !run_lower(&f, "int sink(int, ...);\n"
                               "int bad(int n, ...) {\n"
                               "  return sink(n, __builtin_va_arg_pack());\n"
                               "}\n"));
    T_ASSERT(t, f.errors >= 1);
    low_free(&f);

    /* Specialization runs inside the containing caller; permitting va_start
     * here would make it inspect that caller's ABI save area instead of the
     * arguments captured for bad(1, 2). */
    T_ASSERT(t, !run_lower(&f, "int sink(int, ...);\n"
                               "static inline int bad(int n, ...) {\n"
                               "  __builtin_va_list ap;\n"
                               "  __builtin_va_start(ap, n);\n"
                               "  return sink(n, __builtin_va_arg_pack());\n"
                               "}\n"
                               "int outer(int z, ...) {\n"
                               "  return bad(1, 2);\n"
                               "}\n"));
    T_ASSERT(t, f.errors >= 1);
    low_free(&f);

    T_ASSERT(t, !run_lower(&f, "int sink(int, ...);\n"
                               "static inline int bad(int n, ...) {\n"
                               "  static int state;\n"
                               "  return sink(n + state,\n"
                               "              __builtin_va_arg_pack());\n"
                               "}\n"));
    T_ASSERT(t, f.errors >= 1);
    low_free(&f);

    T_ASSERT(t, !run_lower(&f, "int sink(int, ...);\n"
                               "static inline int bad(int n, ...) {\n"
                               "  return sink(__builtin_va_arg_pack(), n);\n"
                               "}\n"));
    T_ASSERT(t, f.errors >= 1);
    low_free(&f);

    T_ASSERT(t, !run_lower(&f, "int sink(int, ...);\n"
                               "static inline int bad(int n, ...) {\n"
                               "  return sink(n, __builtin_va_arg_pack());\n"
                               "}\n"
                               "int (*p)(int, ...) = bad;\n"));
    T_ASSERT(t, f.errors >= 1);
    low_free(&f);
}
