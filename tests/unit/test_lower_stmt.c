#include <string.h>

#include "lower/lower.h"
#include "parse/parse.h"
#include "unit.h"
#include "util/arena.h"

/* Lowering units, statement side: loop shapes, the LoopCtx stack under
 * nesting, the two-pass switch (Duff included), goto, dead-code cleanup,
 * and the local-initializer walk. */

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
} StFix;

static void st_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    StFix *f = user;

    (void)dc;
    if (d->level >= DIAG_ERROR)
        f->errors++;
}

VEC_DECL(PpVecT, PpToken);

static bool run_lower_s(StFix *f, const char *src)
{
    DiagSink sink;
    SourceFile *sf;
    PpVecT pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec target;
    AstNode *tu;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = st_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, f->dc, &f->in);

    memset(&lang, 0, sizeof(lang));
    lang.std = STD_C17;
    target.kind = CGF_TARGET_X86_64_LINUX_GNU;

    sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t))
        PpVecT_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, target, &f->arena);
    PpVecT_free(&pv);

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

static void st_free(StFix *f)
{
    if (f->text.data)
        buf_free(&f->text);
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

static const char *stxt(StFix *f)
{
    return (const char *)f->text.data;
}

static int scount(const char *hay, const char *needle)
{
    int n = 0;
    size_t len = strlen(needle);

    while ((hay = strstr(hay, needle)) != NULL) {
        n++;
        hay += len;
    }
    return n;
}

void test_lower_loop_shapes(TestCtx *t)
{
    StFix f;

    /* while: condbr in the header; do: condbr at the bottom; for(;;):
     * an unconditional br, NO compare. */
    T_ASSERT(t, run_lower_s(&f, "int f(int n) {\n"
                                "  int a = 0;\n"
                                "  while (n > 0) { a += n; n--; }\n"
                                "  do { a++; } while (a < 10);\n"
                                "  for (;;) { break; }\n"
                                "  return a;\n"
                                "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(stxt(&f), "while.head") != NULL);
    T_ASSERT(t, strstr(stxt(&f), "do.cond") != NULL);
    {
        /* for(;;)'s header br carries no condition: the line is exactly
         * `br for.body...` reached from the header. */
        const char *h = strstr(stxt(&f), "for.head");

        T_ASSERT(t, h != NULL);
        T_ASSERT(t, h && strstr(h, "br for.body") != NULL);
    }
    st_free(&f);
}

void test_lower_continue_targets_step(TestCtx *t)
{
    StFix f;

    /* `continue` in a for targets the STEP block, never the header —
     * the step must still run. */
    T_ASSERT(t, run_lower_s(&f, "int f(int n) {\n"
                                "  int a = 0;\n"
                                "  for (int i = 0; i < n; i++) {\n"
                                "    if (i == 2) continue;\n"
                                "    a += i;\n"
                                "  }\n"
                                "  return a;\n"
                                "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    /* Two branches into the step block: the continue and the body end. */
    T_ASSERT(t, scount(stxt(&f), "br for.step") >= 2);
    st_free(&f);
}

void test_lower_statement_source_locations(TestCtx *t)
{
    StFix f;
    IrFunc *fn;
    u32 bi;
    bool saw_if = false, saw_body_store = false, saw_parent_br = false;
    bool saw_return = false, saw_locless_prologue = false;

    T_ASSERT(t, run_lower_s(&f, "int f(int n) {\n"
                                "  int x = n;\n"
                                "  if (x) {\n"
                                "    x = 2;\n"
                                "  }\n"
                                "  return x;\n"
                                "}\n"));
    fn = &f.m->funcs[0];
    for (bi = 0; bi < fn->nblocks; bi++) {
        IrInst *in;

        for (in = fn->blocks[bi].first; in; in = in->next) {
            Span sp = ir_inst_span(f.m, in);

            if (!in->loc) {
                saw_locless_prologue = true;
                continue;
            }
            T_ASSERT_EQ_STR(t, diag_span_path(f.dc, sp), "t.c");
            if (in->op == IR_CONDBR && sp.line == 3)
                saw_if = true;
            if (in->op == IR_STORE && sp.line == 4)
                saw_body_store = true;
            if (in->op == IR_BR && sp.line == 3)
                saw_parent_br = true;
            if (in->op == IR_RET && sp.line == 6)
                saw_return = true;
        }
    }
    T_ASSERT(t, saw_locless_prologue);
    T_ASSERT(t, saw_if);
    T_ASSERT(t, saw_body_store);
    T_ASSERT(t, saw_parent_br); /* nested body restored the if's span */
    T_ASSERT(t, saw_return);
    st_free(&f);
}

void test_lower_loop_condition_step_locations(TestCtx *t)
{
    StFix f;
    IrFunc *fn;
    u32 bi;
    bool saw_while_cond = false, saw_do_cond = false, saw_for_cond = false;
    bool saw_for_step = false;

    T_ASSERT(t, run_lower_s(&f, "int f(int n) {\n"
                                "  int x = 0;\n"
                                "  while (x < n) x++;\n"
                                "  do x--; while (x);\n"
                                "  for (x = 0; x < n; x++) n--;\n"
                                "  return x;\n"
                                "}\n"));
    fn = &f.m->funcs[0];
    for (bi = 0; bi < fn->nblocks; bi++) {
        IrInst *in;

        for (in = fn->blocks[bi].first; in; in = in->next) {
            Span sp = ir_inst_span(f.m, in);

            if (in->op == IR_CONDBR && sp.line == 3)
                saw_while_cond = true;
            if (in->op == IR_CONDBR && sp.line == 4)
                saw_do_cond = true;
            if (in->op == IR_CONDBR && sp.line == 5)
                saw_for_cond = true;
            if (in->op == IR_IADD && sp.line == 5)
                saw_for_step = true;
        }
    }
    T_ASSERT(t, saw_while_cond);
    T_ASSERT(t, saw_do_cond);
    T_ASSERT(t, saw_for_cond);
    T_ASSERT(t, saw_for_step);
    st_free(&f);
}

void test_lower_switch_in_loop_ctx_stack(TestCtx *t)
{
    StFix f;

    /* break inside the switch exits the SWITCH (sw.join); continue there
     * skips the switch's break-only entry and reaches the LOOP. */
    T_ASSERT(t, run_lower_s(&f, "int f(int n) {\n"
                                "  int a = 0;\n"
                                "  while (n > 0) {\n"
                                "    switch (n & 1) {\n"
                                "    case 0: a++; break;\n"
                                "    default: n--; continue;\n"
                                "    }\n"
                                "    n -= 2;\n"
                                "  }\n"
                                "  return a;\n"
                                "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(stxt(&f), "br sw.join") != NULL);
    T_ASSERT(t, strstr(stxt(&f), "br while.head") != NULL);
    st_free(&f);
}

void test_lower_duff_two_pass(TestCtx *t)
{
    StFix f;

    /* Duff's device: the switch table targets case blocks that sit
     * INSIDE the do-while; the module still verifies (the case blocks
     * existed before the loop body lowered — pass one made them). */
    T_ASSERT(t, run_lower_s(&f, "void duff(char *d, const char *s, int n) {\n"
                                "  int r = (n + 7) / 8;\n"
                                "  switch (n & 7) {\n"
                                "  case 0: do { *d++ = *s++;\n"
                                "  case 3: *d++ = *s++;\n"
                                "  case 2: *d++ = *s++;\n"
                                "  case 1: *d++ = *s++;\n"
                                "          } while (--r > 0);\n"
                                "  }\n"
                                "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    /* The terminator's sorted table: 0,1,2,3 in that order. */
    {
        const char *sw = strstr(stxt(&f), "switch i32");
        const char *c0, *c1, *c2, *c3;

        T_ASSERT(t, sw != NULL);
        c0 = sw ? strstr(sw, "0: sw.case") : NULL;
        c1 = sw ? strstr(sw, "1: sw.case") : NULL;
        c2 = sw ? strstr(sw, "2: sw.case") : NULL;
        c3 = sw ? strstr(sw, "3: sw.case") : NULL;
        T_ASSERT(t, c0 && c1 && c2 && c3);
        T_ASSERT(t, c0 < c1 && c1 < c2 && c2 < c3);
    }
    /* The do-loop's back edge exists alongside the case fallthroughs. */
    T_ASSERT(t, strstr(stxt(&f), "do.cond") != NULL);
    st_free(&f);
}

void test_lower_switch_sorted_and_default(TestCtx *t)
{
    StFix f;

    /* Negative and positive cases sort; a missing default targets the
     * join. */
    T_ASSERT(t, run_lower_s(&f, "int f(int x) {\n"
                                "  switch (x) {\n"
                                "  case 5: return 1;\n"
                                "  case -3: return 2;\n"
                                "  case 0: return 3;\n"
                                "  }\n"
                                "  return 4;\n"
                                "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    {
        const char *sw = strstr(stxt(&f), "switch i32");
        const char *m3 = sw ? strstr(sw, "-3: sw.case") : NULL;
        const char *z = sw ? strstr(sw, "0: sw.case") : NULL;
        const char *p5 = sw ? strstr(sw, "5: sw.case") : NULL;

        T_ASSERT(t, sw && strstr(sw, "sw.join") != NULL);
        T_ASSERT(t, m3 && z && p5);
        T_ASSERT(t, m3 < z && z < p5);
    }
    st_free(&f);
}

void test_lower_goto_over_decl(TestCtx *t)
{
    StFix f;

    /* goto past `int x = 5;` leaves x uninitialized — legal; the label
     * blocks exist from the pre-pass and everything verifies. */
    T_ASSERT(t, run_lower_s(&f, "int f(int n) {\n"
                                "  int acc = 0;\n"
                                "  if (n) goto skip;\n"
                                "  acc = 100;\n"
                                "skip:\n"
                                "  return acc;\n"
                                "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(stxt(&f), "L.skip") != NULL);
    st_free(&f);
}

void test_lower_dead_code_deleted(TestCtx *t)
{
    StFix f;

    /* Code after return is unreachable; the cleanup DELETES its blocks
     * (verifier check 6 rejects orphans) and the module still verifies.
     * A label inside the dead tail that a live goto targets stays. */
    T_ASSERT(t, run_lower_s(&f, "int f(void) {\n"
                                "  return 1;\n"
                                "  return 2;\n"
                                "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, f.m->funcs[0].nblocks, 1);
    T_ASSERT(t, strstr(stxt(&f), "ret i32 2") == NULL);
    st_free(&f);
}

void test_lower_local_init_copy_then_store(TestCtx *t)
{
    StFix f;

    /* Sprint 19 thresholds: a 16-byte list with one runtime element gets
     * inline CONSTANT stores first (runtime slot zeroed), then the
     * runtime store AFTER — copy-then-store, never store-then-copy. */
    T_ASSERT(t, run_lower_s(&f, "int f(int v) {\n"
                                "  int a[4] = {1, v};\n"
                                "  return a[3];\n"
                                "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    {
        const char *c1 = strstr(stxt(&f), "store i64 1,");
        const char *rt = c1 ? strstr(c1, "store i32 %") : NULL;

        /* the runtime store follows the constant part (searching FROM
         * the constant store skips the entry-block parameter spill) */
        T_ASSERT(t, c1 != NULL && rt != NULL);
    }
    /* No memset, no template: 16 bytes is under the store threshold. */
    T_ASSERT_EQ_INT(t, scount(stxt(&f), "memset"), 0);
    T_ASSERT_EQ_INT(t, scount(stxt(&f), "memcpy"), 0);
    st_free(&f);
}

void test_lower_init_thresholds(TestCtx *t)
{
    StFix f;

    /* DoD 5: {0} on a big array is EXACTLY one memset; a 48-byte
     * constant list is a deduped .rodata template memcpy; a long zero
     * tail splits into head stores + one tail memset. */
    T_ASSERT(t, run_lower_s(&f, "int a(void) { int c[100] = {0};"
                                " return c[9]; }\n"
                                "int b(void) { int t[12] ="
                                " {1,2,3,4,5,6,7,8,9,10,11,12};"
                                " return t[0]; }\n"
                                "int c(void) { int t[16] = {1,2};"
                                " return t[0]; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(stxt(&f), "memset %0, 0, 400") != NULL);
    T_ASSERT(t, strstr(stxt(&f), "global @.Lconst.0 size 48") != NULL);
    T_ASSERT(t, strstr(stxt(&f), "memcpy %0, @.Lconst.0, 48") != NULL);
    /* the zero-tail split: the run swallows 2's high bytes (59 = 64-5) */
    T_ASSERT(t, strstr(stxt(&f), ", 0, 59") != NULL);
    T_ASSERT_EQ_INT(t, scount(stxt(&f), "memset"), 2);
    st_free(&f);
}

void test_lower_string_pool_dedup(TestCtx *t)
{
    StFix f;

    /* Content-keyed dedup: two occurrences of "hi" share ONE symbol;
     * a different literal gets the next slot, in first-occurrence order. */
    T_ASSERT(t, run_lower_s(&f, "const char *a(void) { return \"hi\"; }\n"
                                "const char *b(void) { return \"hi\"; }\n"
                                "const char *c(void) { return \"ho\"; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, scount(stxt(&f), "global @.Lstr."), 2);
    T_ASSERT(t, strstr(stxt(&f), "global @.Lstr.0 size 3 align 1 internal "
                                 "init x686900") != NULL);
    T_ASSERT(t, strstr(stxt(&f), "global @.Lstr.1 size 3 align 1 internal "
                                 "init x686f00") != NULL);
    st_free(&f);
}

void test_lower_designators_and_union_init(TestCtx *t)
{
    StFix f;

    T_ASSERT(t, run_lower_s(&f, "struct P { int x, y, z; };\n"
                                "int f(int v) {\n"
                                "  struct P p = {.z = v, .x = 1};\n"
                                "  int a[6] = {[4] = v, 9};\n"
                                "  return p.z + a[5];\n"
                                "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    /* .z (runtime v) stores at offset 8 after the constant stores; the
     * follower 9 after [4]=v sits at byte 20 INSIDE the constant image
     * (chunk stores cover it). Both runtime stores land last. */
    T_ASSERT(t, strstr(stxt(&f), "ptradd %") != NULL);
    T_ASSERT(t, strstr(stxt(&f), ", 8") != NULL);
    T_ASSERT_EQ_INT(t, scount(stxt(&f), "memset"), 0);
    st_free(&f);
}

void test_lower_string_init_memcpy(TestCtx *t)
{
    StFix f;

    T_ASSERT(t, run_lower_s(&f, "int f(void) {\n"
                                "  char buf[8] = \"hi\";\n"
                                "  return buf[0];\n"
                                "}\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    /* Sprint 19: an 8-byte char array from a literal is ONE i64 store of
     * the bytes ("hello\0\0\0" little-endian) — no .rodata object, no
     * memcpy, no memset. */
    T_ASSERT(t, strstr(stxt(&f), "store i64 26984,") != NULL);
    T_ASSERT_EQ_INT(t, scount(stxt(&f), "memcpy"), 0);
    T_ASSERT_EQ_INT(t, scount(stxt(&f), ".Lstr."), 0);
    st_free(&f);
}

void test_lower_local_static_mangled(TestCtx *t)
{
    StFix f;

    /* Two statics with one NAME in different functions get distinct
     * mangled globals, deterministically numbered. */
    T_ASSERT(t, run_lower_s(&f,
                            "int f(void) { static int n = 1; return n++; }\n"
                            "int g(void) { static int n = 2; return n++; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(stxt(&f), "global @n.0 size 4 align 4 internal "
                                 "init x01000000") != NULL);
    T_ASSERT(t, strstr(stxt(&f), "global @n.1 size 4 align 4 internal "
                                 "init x02000000") != NULL);
    st_free(&f);
}

void test_lower_vla_live(TestCtx *t)
{
    StFix f;

    /* Flipped in Sprint 20: dynamic alloca with the evaluated size. */
    T_ASSERT(t, run_lower_s(&f, "int f(int n) { int a[n]; return 0; }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(stxt(&f), "stacksave") != NULL);
    T_ASSERT(t, strstr(stxt(&f), "imul i64") != NULL);
    st_free(&f);
}

void test_lower_fallthrough_returns(TestCtx *t)
{
    StFix f;

    /* Falling off the end: main returns 0; void returns; a non-void
     * non-main returns undef (the caller must not look). */
    T_ASSERT(t, run_lower_s(&f, "void v(void) { }\n"
                                "int falls(int n) { if (n) return 1; }\n"
                                "int main(void) { }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT(t, strstr(stxt(&f), "ret i32 undef") != NULL);
    {
        const char *mn = strstr(stxt(&f), "func i32 @main");

        T_ASSERT(t, mn != NULL);
        T_ASSERT(t, mn && strstr(mn, "ret i32 0") != NULL);
    }
    st_free(&f);
}

void test_lower_inline_def_not_emitted(TestCtx *t)
{
    StFix f;

    /* Sprint 16's matrix consumed: an inline definition (no extern
     * declaration anywhere) emits NOTHING from this TU; calls go through
     * the symbol. */
    T_ASSERT(t, run_lower_s(&f, "inline int twice(int x) { return 2 * x; }\n"
                                "int f(int v) { return twice(v); }\n"));
    T_ASSERT(t, ir_verify(f.dc, f.m));
    T_ASSERT_EQ_INT(t, f.m->nfuncs, 1);
    T_ASSERT(t, strstr(stxt(&f), "func i32 @twice") == NULL);
    T_ASSERT(t, strstr(stxt(&f), "call i32 @twice(") != NULL);
    st_free(&f);
}
