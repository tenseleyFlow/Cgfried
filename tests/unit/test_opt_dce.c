#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} DceFix;

bool opt_dce(IrModule *m, const OptConfig *cfg);

static void silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void fix_init(DceFix *f)
{
    DiagSink sink = {silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *parse(DceFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<dce-test>");
}

static u32 count_op(const IrModule *m, IrOp op)
{
    const IrFunc *fn = &m->funcs[0];
    u32 bi, n = 0;

    for (bi = 0; bi < fn->nblocks; bi++) {
        const IrInst *in;

        for (in = fn->blocks[bi].first; in; in = in->next)
            n += in->op == op;
    }
    return n;
}

void test_opt_dce_removes_dead_chain_and_keeps_live_block_argument(TestCtx *t)
{
    DceFix f;
    IrModule *m;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func i32 @f(i32 %x) {\n"
                  "entry():\n"
                  "    %live = iadd i32 %x, 1\n"
                  "    %dead1 = imul i32 %x, 3\n"
                  "    %dead2 = isub i32 %dead1, 4\n"
                  "    br out(i32 %live)\n"
                  "out(i32 %v):\n"
                  "    ret i32 %v\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, m && opt_dce(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_IADD), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_IMUL), 0);
        T_ASSERT_EQ_INT(t, count_op(m, IR_ISUB), 0);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_dce_preserves_every_side_effect_root(TestCtx *t)
{
    DceFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[256];
    size_t n;

    fix_init(&f);
    m = parse(&f, "func void @f(ptr %p, ...) {\n"
                  "entry():\n"
                  "    %q = alloca 16, align 8\n"
                  "    store i32 1, %q, align 4\n"
                  "    %vl = load i32, %p, align 4, volatile\n"
                  "    memset %q, 0, 16, align 1\n"
                  "    memcpy %q, %p, 8, align 8\n"
                  "    %cr = call i32 @side(ptr %q)\n"
                  "    va_start %q\n"
                  "    %tok = stacksave\n"
                  "    stackrestore %tok\n"
                  "    %ar = atomicrmw add i32 %p, 1, seq_cst\n"
                  "    %cx = cmpxchg i32 %p, 1, 2, seq_cst\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O1);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_dce(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_STORE), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_LOAD), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_MEMSET), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_MEMCPY), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_CALL), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_VA_START), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_STACKSAVE), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_STACKRESTORE), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_ATOMICRMW), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_CMPXCHG), 1);
    }
    if (report) {
        fflush(report);
        rewind(report);
        n = fread(log, 1, sizeof(log) - 1, report);
        log[n] = '\0';
        T_ASSERT(t, strstr(log, "dce_call_side_effects") != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}
