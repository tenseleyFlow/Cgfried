#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"
#include "util/buf.h"

typedef struct StrengthFix {
    Arena arena;
    DiagCtx *dc;
} StrengthFix;

static void strength_silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void strength_fix_init(StrengthFix *fix)
{
    DiagSink sink = {strength_silent_sink, NULL};

    arena_init(&fix->arena);
    fix->dc = diag_ctx_new(&fix->arena);
    diag_set_sink(fix->dc, sink);
}

static IrModule *strength_parse(StrengthFix *fix, const char *src)
{
    return ir_parse_module(&fix->arena, fix->dc, src, "<strength-test>");
}

static char *strength_print(IrModule *m, Buf *out)
{
    buf_init(out);
    ir_print_module_buf(out, m);
    buf_push_u8(out, 0);
    return (char *)out->data;
}

static u32 block_op_count(const IrFunc *f, const char *name, IrOp op)
{
    u32 bi, count = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        if (!f->blocks[bi].name || strcmp(f->blocks[bi].name, name) != 0)
            continue;
        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == op)
                count++;
    }
    return count;
}

static const IrBlock *named_block(const IrFunc *f, const char *name)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++)
        if (f->blocks[bi].name && strcmp(f->blocks[bi].name, name) == 0)
            return &f->blocks[bi];
    return NULL;
}

static void read_report(FILE *report, char *out, size_t cap)
{
    size_t n;

    fflush(report);
    rewind(report);
    n = fread(out, 1, cap - 1, report);
    out[n] = '\0';
}

void test_opt_strength_adds_affine_accumulator(TestCtx *t)
{
    StrengthFix fix;
    IrModule *m;
    OptConfig cfg;
    const IrBlock *loop;
    Buf text;
    char *printed;

    strength_fix_init(&fix);
    m = strength_parse(&fix, "func i32 @f(i32 %n, i32 %k) {\n"
                             "entry():\n"
                             "    br loop(i32 0)\n"
                             "loop(i32 %i):\n"
                             "    %more = icmp slt i32 %i, %n\n"
                             "    condbr %more, body(), exit(i32 %i)\n"
                             "body():\n"
                             "    %scaled = imul i32 %i, %k\n"
                             "    %used = iadd i32 %scaled, 9\n"
                             "    %next = iadd i32 %i, 1\n"
                             "    br loop(i32 %next)\n"
                             "exit(i32 %result):\n"
                             "    ret i32 %result\n"
                             "}\n");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_strength(m, &cfg));
    if (m) {
        loop = named_block(&m->funcs[0], "loop");
        T_ASSERT(t, loop != NULL);
        if (loop)
            T_ASSERT_EQ_INT(t, loop->nparams, 2);
        T_ASSERT_EQ_INT(t, block_op_count(&m->funcs[0], "body", IR_IMUL), 0);
        T_ASSERT_EQ_INT(t, block_op_count(&m->funcs[0], "body", IR_IADD), 3);
        T_ASSERT(t, ir_verify(fix.dc, m));
        printed = strength_print(m, &text);
        T_ASSERT(t, strstr(printed, "loop(i32 %") != NULL);
        T_ASSERT(t, strstr(printed, "br loop(i32 %") != NULL);
        buf_free(&text);
    }
    arena_free_all(&fix.arena);
}

void test_opt_strength_constant_math_wraps_at_original_width(TestCtx *t)
{
    StrengthFix fix;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *printed;

    strength_fix_init(&fix);
    m = strength_parse(&fix, "func i8 @f() {\n"
                             "entry():\n"
                             "    br loop(i8 250)\n"
                             "loop(i8 %i):\n"
                             "    %scaled = imul i8 %i, 3\n"
                             "    %next = iadd i8 %i, 10\n"
                             "    %stop = icmp eq i8 %next, 24\n"
                             "    condbr %stop, exit(i8 %i), loop(i8 %next)\n"
                             "exit(i8 %result):\n"
                             "    ret i8 %result\n"
                             "}\n");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.fwrapv = true;
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_strength(m, &cfg));
    if (m) {
        printed = strength_print(m, &text);
        T_ASSERT(t, strstr(printed, "br loop(i8 250, i8 238)") != NULL);
        T_ASSERT(t, strstr(printed, "iadd i8 %") != NULL);
        T_ASSERT(t, strstr(printed, ", 30") != NULL);
        T_ASSERT(t, ir_verify(fix.dc, m));
        buf_free(&text);
    }
    arena_free_all(&fix.arena);
}

void test_opt_strength_exhausts_sites_across_functions(TestCtx *t)
{
    StrengthFix fix;
    IrModule *m;
    OptConfig cfg;

    strength_fix_init(&fix);
    m = strength_parse(&fix, "func i32 @f() {\n"
                             "entry():\n"
                             "    br loop(i32 0)\n"
                             "loop(i32 %i):\n"
                             "    %a = imul i32 %i, 3\n"
                             "    %b = imul i32 %i, 5\n"
                             "    %next = iadd i32 %i, 1\n"
                             "    %done = icmp eq i32 %next, 4\n"
                             "    condbr %done, exit(), loop(i32 %next)\n"
                             "exit():\n"
                             "    ret i32 0\n"
                             "}\n"
                             "func i32 @g() {\n"
                             "entry():\n"
                             "    br loop(i32 0)\n"
                             "loop(i32 %i):\n"
                             "    %a = imul i32 %i, 7\n"
                             "    %b = imul i32 %i, 9\n"
                             "    %next = iadd i32 %i, 1\n"
                             "    %done = icmp eq i32 %next, 4\n"
                             "    condbr %done, exit(), loop(i32 %next)\n"
                             "exit():\n"
                             "    ret i32 0\n"
                             "}\n"
                             "func i32 @h(i32 %n) {\n"
                             "entry():\n"
                             "    br loop(i32 1)\n"
                             "loop(i32 %i):\n"
                             "    %next = imul i32 %i, %i\n"
                             "    %more = icmp slt i32 %next, %n\n"
                             "    condbr %more, loop(i32 %next), exit()\n"
                             "exit():\n"
                             "    ret i32 0\n"
                             "}\n");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_strength(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, block_op_count(&m->funcs[0], "loop", IR_IMUL), 0);
        T_ASSERT_EQ_INT(t, block_op_count(&m->funcs[1], "loop", IR_IMUL), 0);
        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT(t, !opt_strength(m, &cfg));
    }
    arena_free_all(&fix.arena);
}

void test_opt_strength_bails_on_widened_wrap_sensitive_iv(TestCtx *t)
{
    StrengthFix fix;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[512];

    strength_fix_init(&fix);
    m = strength_parse(&fix, "func i32 @f(i8 %n) {\n"
                             "entry():\n"
                             "    br loop(i8 0)\n"
                             "loop(i8 %i):\n"
                             "    %wide = zext i8 %i to i32\n"
                             "    %scaled = imul i32 %wide, 7\n"
                             "    %next = iadd i8 %i, 1\n"
                             "    %more = icmp ult i8 %next, %n\n"
                             "    condbr %more, loop(i8 %next), exit()\n"
                             "exit():\n"
                             "    ret i32 0\n"
                             "}\n");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.fwrapv = true;
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_strength(m, &cfg));
    if (report) {
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "bail: strength sr_wrap func=@f") != NULL);
        fclose(report);
    }
    if (m)
        T_ASSERT(t, ir_verify(fix.dc, m));
    arena_free_all(&fix.arena);
}

void test_opt_strength_descriptor(TestCtx *t)
{
    T_ASSERT_EQ_STR(t, OPT_PASS_STRENGTH.name, "strength");
    T_ASSERT(t, OPT_PASS_STRENGTH.run == opt_strength);
    T_ASSERT_EQ_INT(t, OPT_PASS_STRENGTH.pinned_policy, PASS_PINNED_EXACT);
}
