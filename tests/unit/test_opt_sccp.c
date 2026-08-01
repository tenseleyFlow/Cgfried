#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"
#include "util/buf.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} SccpFix;

static void sccp_silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void sccp_fix_init(SccpFix *f)
{
    DiagSink sink = {sccp_silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *sccp_parse(SccpFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<sccp-test>");
}

static char *sccp_print(IrModule *m, Buf *out)
{
    buf_init(out);
    ir_print_module_buf(out, m);
    buf_push_u8(out, 0);
    return (char *)out->data;
}

static void sccp_read_report(FILE *report, char *out, size_t cap)
{
    size_t n;

    fflush(report);
    rewind(report);
    n = fread(out, 1, cap - 1, report);
    out[n] = '\0';
}

void test_opt_sccp_constant_edge_meets_only_executable_predecessor(TestCtx *t)
{
    SccpFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;

    sccp_fix_init(&f);
    m = sccp_parse(&f, "func i32 @f() {\n"
                       "entry():\n"
                       "    %c = icmp eq i32 4, 4\n"
                       "    condbr %c, yes(), no()\n"
                       "yes():\n"
                       "    br join(i32 11)\n"
                       "no():\n"
                       "    br join(i32 22)\n"
                       "join(i32 %x):\n"
                       "    ret i32 %x\n"
                       "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_sccp(m, &cfg));
    if (m) {
        s = sccp_print(m, &text);
        T_ASSERT(t, strstr(s, "condbr") == NULL);
        T_ASSERT(t, strstr(s, "no(") == NULL);
        T_ASSERT(t, strstr(s, "ret i32 11") != NULL);
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_opt_sccp_overdefined_condition_keeps_both_edges(TestCtx *t)
{
    SccpFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;

    sccp_fix_init(&f);
    m = sccp_parse(&f, "func i32 @f(i32 %c) {\n"
                       "entry():\n"
                       "    condbr %c, yes(), no()\n"
                       "yes():\n"
                       "    ret i32 1\n"
                       "no():\n"
                       "    ret i32 0\n"
                       "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, m && !opt_sccp(m, &cfg));
    if (m) {
        s = sccp_print(m, &text);
        T_ASSERT(t, strstr(s, "condbr %0, yes(), no()") != NULL);
        T_ASSERT_EQ_INT(t, m->funcs[0].nblocks, 3);
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_opt_sccp_undef_condition_keeps_both_edges_and_logs(TestCtx *t)
{
    SccpFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    FILE *report;
    char *s;
    char log[512];

    sccp_fix_init(&f);
    m = sccp_parse(&f, "func i32 @f() {\n"
                       "entry():\n"
                       "    condbr undef, yes(), no()\n"
                       "yes():\n"
                       "    ret i32 1\n"
                       "no():\n"
                       "    ret i32 0\n"
                       "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O1);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_sccp(m, &cfg));
    if (m && report) {
        sccp_read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "undef_branch") != NULL);
        s = sccp_print(m, &text);
        T_ASSERT(t, strstr(s, "condbr undef, yes(), no()") != NULL);
        T_ASSERT_EQ_INT(t, m->funcs[0].nblocks, 3);
        buf_free(&text);
    }
    if (report)
        fclose(report);
    arena_free_all(&f.arena);
}

void test_opt_sccp_loop_parameter_waits_for_executable_edges(TestCtx *t)
{
    SccpFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;

    sccp_fix_init(&f);
    m = sccp_parse(&f, "func void @f() {\n"
                       "entry():\n"
                       "    br loop(i32 7)\n"
                       "loop(i32 %x):\n"
                       "    %c = icmp eq i32 %x, 7\n"
                       "    condbr %c, body(), dead()\n"
                       "body():\n"
                       "    br loop(i32 7)\n"
                       "dead():\n"
                       "    ret\n"
                       "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_sccp(m, &cfg));
    if (m) {
        s = sccp_print(m, &text);
        T_ASSERT(t, strstr(s, "condbr") == NULL);
        T_ASSERT(t, strstr(s, "dead(") == NULL);
        T_ASSERT(t, strstr(s, "loop(i32") != NULL);
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_opt_sccp_prunes_large_constant_dead_arm(TestCtx *t)
{
    enum { NDEAD = 4095 };
    SccpFix f;
    IrModule *m;
    IrFunc *fn;
    BlockId entry, live;
    BlockId *dead;
    IrBuilder b;
    IrOperand condition;
    OptConfig cfg;
    u32 i;

    sccp_fix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    fn = ir_func_new(m, "large_cfg", IRT_VOID, NULL, 0);
    entry = ir_block_new(m, fn, "entry");
    live = ir_block_new(m, fn, "live");
    dead = arena_alloc(&f.arena, NDEAD * sizeof(*dead), _Alignof(BlockId));
    for (i = 0; i < NDEAD; i++)
        dead[i] = ir_block_new(m, fn, "dead");

    ir_builder_at(&b, m, fn, entry);
    condition = ir_op_iconst(IRT_I32, 1);
    ir_build_condbr(&b, condition, live, NULL, 0, dead[0], NULL, 0);
    ir_builder_at(&b, m, fn, live);
    ir_build_ret(&b, NULL);
    for (i = 0; i < NDEAD; i++) {
        ir_builder_at(&b, m, fn, dead[i]);
        if (i + 1 < NDEAD)
            ir_build_br(&b, dead[i + 1], NULL, 0);
        else
            ir_build_ret(&b, NULL);
    }

    T_ASSERT_EQ_INT(t, fn->nblocks, 4097);
    T_ASSERT(t, ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, opt_sccp(m, &cfg));
    T_ASSERT_EQ_INT(t, fn->nblocks, 2);
    T_ASSERT(t, ir_verify(f.dc, m));
    arena_free_all(&f.arena);
}

void test_opt_sccp_constant_switch_rewrite_roundtrips(TestCtx *t)
{
    SccpFix f;
    IrModule *m;
    IrModule *roundtrip;
    OptConfig cfg;
    Buf text;
    char *s;

    sccp_fix_init(&f);
    m = sccp_parse(&f, "func i32 @f() {\n"
                       "entry():\n"
                       "    switch i32 1, join(i32 0), 1: join(i32 5), "
                       "2: join(i32 7)\n"
                       "join(i32 %x):\n"
                       "    ret i32 %x\n"
                       "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_sccp(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, m->funcs[0].blocks[0].last->op, IR_BR);
        T_ASSERT(t, m->funcs[0].blocks[0].last->edges[0].case_val == 0);
        s = sccp_print(m, &text);
        T_ASSERT(t, strstr(s, "ret i32 5") != NULL);
        roundtrip = sccp_parse(&f, s);
        T_ASSERT(t, roundtrip != NULL && ir_module_struct_eq(m, roundtrip));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}
