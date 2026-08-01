#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"
#include "util/buf.h"

extern const Pass OPT_PASS_JUMP_THREAD;
bool opt_jump_thread(IrModule *m, const OptConfig *cfg);

typedef struct {
    Arena arena;
    DiagCtx *dc;
} JtFix;

static void jt_silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void jt_fix_init(JtFix *f)
{
    DiagSink sink = {jt_silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *jt_parse(JtFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<jump-thread-test>");
}

static char *jt_print(IrModule *m, Buf *out)
{
    buf_init(out);
    ir_print_module_buf(out, m);
    buf_push_u8(out, 0);
    return (char *)out->data;
}

static u32 jt_count_op(const IrModule *m, IrOp op)
{
    u32 fi, bi, n = 0;

    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            const IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next)
                if (in->op == op)
                    n++;
        }
    return n;
}

static void jt_read_report(FILE *report, char *out, size_t cap)
{
    size_t n;

    fflush(report);
    rewind(report);
    n = fread(out, 1, cap - 1, report);
    out[n] = '\0';
}

void test_opt_jump_thread_correlated_condition_preserves_edge_args(TestCtx *t)
{
    JtFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;

    jt_fix_init(&f);
    m = jt_parse(&f, "func i32 @f(i32 %c, i32 %x) {\n"
                     "entry():\n"
                     "    %f0 = iadd i32 %x, 1\n"
                     "    %f1 = iadd i32 %f0, 1\n"
                     "    %f2 = iadd i32 %f1, 1\n"
                     "    %f3 = iadd i32 %f2, 1\n"
                     "    %f4 = iadd i32 %f3, 1\n"
                     "    %f5 = iadd i32 %f4, 1\n"
                     "    %f6 = iadd i32 %f5, 1\n"
                     "    %f7 = iadd i32 %f6, 1\n"
                     "    %f8 = iadd i32 %f7, 1\n"
                     "    condbr %c, "
                     "middle_with_a_label_longer_than_the_old_fixed_buffer_"
                     "abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ_"
                     "0123456789(i32 %x), jt.5.0()\n"
                     "middle_with_a_label_longer_than_the_old_fixed_buffer_"
                     "abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ_"
                     "0123456789(i32 %p):\n"
                     "    %v = iadd i32 %p, 7\n"
                     "    condbr %c, yes(i32 %v), jt.5.0()\n"
                     "yes(i32 %r):\n"
                     "    ret i32 %r\n"
                     "jt.5.0():\n"
                     "    ret i32 0\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_jump_thread(m, &cfg));
    if (m) {
        IrModule *round;

        s = jt_print(m, &text);
        T_ASSERT_EQ_INT(t, jt_count_op(m, IR_CONDBR), 1);
        T_ASSERT(t, strstr(s, "jt.5.1") != NULL);
        T_ASSERT(t, strstr(s, "br yes(i32") != NULL);
        T_ASSERT(t, ir_verify(f.dc, m));
        round = ir_parse_module(&f.arena, f.dc, s, "<jump-thread-roundtrip>");
        T_ASSERT(t, round != NULL && ir_module_struct_eq(m, round));
        T_ASSERT(t, !opt_jump_thread(m, &cfg));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_opt_jump_thread_refuses_effectful_intermediate_block(TestCtx *t)
{
    JtFix f;
    IrModule *m;
    OptConfig cfg;

    jt_fix_init(&f);
    m = jt_parse(&f, "func i32 @f(i32 %c, ptr %p) {\n"
                     "entry():\n"
                     "    condbr %c, mid(), no()\n"
                     "mid():\n"
                     "    store i32 1, %p, align 4, volatile\n"
                     "    condbr %c, yes(), no()\n"
                     "yes():\n"
                     "    ret i32 1\n"
                     "no():\n"
                     "    ret i32 0\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    T_ASSERT(t, m && !opt_jump_thread(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, jt_count_op(m, IR_CONDBR), 2);
        T_ASSERT_EQ_INT(t, jt_count_op(m, IR_STORE), 1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

static char *jt_boundary_source(Buf *src, u32 middle_count)
{
    u32 i;

    buf_init(src);
    buf_printf(src, "func i32 @f(i32 %%c, i32 %%x) {\nentry():\n");
    for (i = 0; i < 80; i++)
        buf_printf(src, "    %%f%u = iadd i32 %%x, %u\n", i, i + 1);
    buf_printf(src, "    condbr %%c, mid(), no()\nmid():\n");
    for (i = 0; i < middle_count; i++)
        buf_printf(src, "    %%m%u = iadd i32 %%x, %u\n", i, i + 101);
    buf_printf(src, "    condbr %%c, yes(), no()\nyes():\n"
                    "    ret i32 1\nno():\n    ret i32 0\n}\n");
    buf_push_u8(src, 0);
    return (char *)src->data;
}

void test_opt_jump_thread_clone_size_boundary_11_and_12(TestCtx *t)
{
    u32 count;

    for (count = 11; count <= 12; count++) {
        JtFix f;
        IrModule *m;
        OptConfig cfg;
        Buf src;

        jt_fix_init(&f);
        m = jt_parse(&f, jt_boundary_source(&src, count));
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        opt_config_init(&cfg, OPT_O2);
        cfg.verify_after_each = true;
        if (count == 11) {
            T_ASSERT(t, m && opt_jump_thread(m, &cfg));
            if (m)
                T_ASSERT_EQ_INT(t, jt_count_op(m, IR_CONDBR), 1);
        } else {
            T_ASSERT(t, m && !opt_jump_thread(m, &cfg));
            if (m)
                T_ASSERT_EQ_INT(t, jt_count_op(m, IR_CONDBR), 2);
        }
        if (m)
            T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&src);
        arena_free_all(&f.arena);
    }
}

void test_opt_jump_thread_cumulative_growth_cap_is_stable(TestCtx *t)
{
    JtFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[512];

    jt_fix_init(&f);
    m = jt_parse(&f, "func i32 @f(i32 %c, i32 %d, i32 %x) {\n"
                     "entry():\n"
                     "    %f0 = iadd i32 %x, 1\n"
                     "    %f1 = iadd i32 %f0, 1\n"
                     "    %f2 = iadd i32 %f1, 1\n"
                     "    %f3 = iadd i32 %f2, 1\n"
                     "    %f4 = iadd i32 %f3, 1\n"
                     "    condbr %c, first(), out0()\n"
                     "first():\n"
                     "    %a = iadd i32 %x, 10\n"
                     "    condbr %c, next(), out1()\n"
                     "next():\n"
                     "    condbr %d, second(), out2()\n"
                     "second():\n"
                     "    %b = iadd i32 %x, 20\n"
                     "    condbr %d, yes(), out3()\n"
                     "yes():\n    ret i32 1\n"
                     "out0():\n    ret i32 0\n"
                     "out1():\n    ret i32 0\n"
                     "out2():\n    ret i32 0\n"
                     "out3():\n    ret i32 0\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.bail_log = true;
    cfg.report = report;
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_jump_thread(m, &cfg));
    if (m) {
        jt_read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "jt_growth_cap") != NULL);
        T_ASSERT_EQ_INT(t, jt_count_op(m, IR_CONDBR), 3);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    if (report)
        fclose(report);
    arena_free_all(&f.arena);
}

void test_opt_jump_thread_irreducible_refusal_is_byte_identical(TestCtx *t)
{
    JtFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    Buf before, after;
    char log[512];

    jt_fix_init(&f);
    m = jt_parse(&f, "func i32 @f(i32 %c) {\n"
                     "entry():\n"
                     "    condbr %c, header(), other()\n"
                     "other():\n"
                     "    br header()\n"
                     "header():\n"
                     "    condbr %c, body(), exit()\n"
                     "body():\n"
                     "    br header()\n"
                     "exit():\n"
                     "    ret i32 0\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    if (m)
        (void)jt_print(m, &before);
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.bail_log = true;
    cfg.report = report;
    cfg.verify_after_each = true;
    T_ASSERT(t, m && !opt_jump_thread(m, &cfg));
    if (m) {
        char *post = jt_print(m, &after);

        jt_read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "jt_would_create_irreducible") != NULL);
        T_ASSERT_EQ_STR(t, (char *)before.data, post);
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&before);
        buf_free(&after);
    }
    if (report)
        fclose(report);
    arena_free_all(&f.arena);
}

void test_opt_jump_thread_keeps_natural_loop_reducible(TestCtx *t)
{
    JtFix f;
    IrModule *m;
    OptConfig cfg;

    jt_fix_init(&f);
    m = jt_parse(&f, "func i32 @f(i32 %c, i32 %d) {\n"
                     "entry():\n"
                     "    %pad = iadd i32 %c, 0\n"
                     "    condbr %c, mid(), no()\n"
                     "mid():\n"
                     "    condbr %c, header(), no()\n"
                     "header():\n"
                     "    condbr %d, body(), exit()\n"
                     "body():\n"
                     "    br header()\n"
                     "exit():\n"
                     "    ret i32 1\n"
                     "no():\n"
                     "    ret i32 0\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_jump_thread(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, jt_count_op(m, IR_CONDBR), 2);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_jump_thread_exports_pass_descriptor(TestCtx *t)
{
    T_ASSERT_EQ_STR(t, OPT_PASS_JUMP_THREAD.name, "jump_thread");
    T_ASSERT(t, OPT_PASS_JUMP_THREAD.run == opt_jump_thread);
}
