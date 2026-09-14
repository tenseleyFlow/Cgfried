#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"
#include "util/buf.h"

typedef void (*ChildAction)(void);

static void silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static IrModule *minimal_module(Arena *arena)
{
    DiagCtx *dc = diag_ctx_new(arena);
    DiagSink sink = {silent_sink, NULL};
    IrModule *m;
    IrFunc *f;
    IrBuilder b;
    BlockId entry;
    IrOperand zero;

    diag_set_sink(dc, sink);
    m = ir_module_new(arena, dc);
    f = ir_func_new(m, "f", IRT_I32, NULL, 0);
    entry = ir_block_new(m, f, "entry");
    ir_builder_at(&b, m, f, entry);
    zero = ir_op_iconst(IRT_I32, 0);
    ir_build_ret(&b, &zero);
    return m;
}

void test_opt_config_fast_math_is_ofast_only(TestCtx *t)
{
    OptConfig cfg;

    opt_config_init(&cfg, OPT_O3);
    T_ASSERT(t, !cfg.fast_math.reassoc);
    T_ASSERT(t, !cfg.fast_math.no_nans);
    T_ASSERT(t, !cfg.fast_math.no_infs);
    T_ASSERT(t, !cfg.fast_math.no_signed_zeros);
    T_ASSERT(t, !cfg.fast_math.reciprocal_math);

    opt_config_init(&cfg, OPT_OFAST);
    T_ASSERT(t, cfg.fast_math.reassoc);
    T_ASSERT(t, cfg.fast_math.no_nans);
    T_ASSERT(t, cfg.fast_math.no_infs);
    T_ASSERT(t, cfg.fast_math.no_signed_zeros);
    T_ASSERT(t, cfg.fast_math.reciprocal_math);
}

static bool dishonest_mutation(IrModule *m, const OptConfig *cfg)
{
    (void)cfg;
    m->funcs[0].name = "mutated";
    return false;
}

static bool never_converges(IrModule *m, const OptConfig *cfg)
{
    IrInst *ret = m->funcs[0].blocks[0].last;

    (void)cfg;
    ret->ops[0].a ^= 1;
    return true;
}

static bool shrink_one_instruction(IrModule *m, const OptConfig *cfg)
{
    IrFunc *f = &m->funcs[0];
    IrBlock *block = &f->blocks[0];
    IrInst *in = block->first;
    IrInst *prev = NULL;

    (void)cfg;
    while (in) {
        if (in->op == IR_IADD) {
            if (prev)
                prev->next = in->next;
            else
                block->first = in->next;
            block->ninsts--;
            ir_func_renumber(m->arena, f);
            return true;
        }
        prev = in;
        in = in->next;
    }
    return false;
}

static IrInst *structural_oscillator_inst;

static bool oscillate_structural_size(IrModule *m, const OptConfig *cfg)
{
    IrFunc *f = &m->funcs[0];
    IrBlock *block = &f->blocks[0];

    (void)cfg;
    if (block->first->op == IR_IADD) {
        structural_oscillator_inst = block->first;
        block->first = block->first->next;
        block->ninsts--;
    } else {
        structural_oscillator_inst->next = block->first;
        block->first = structural_oscillator_inst;
        block->ninsts++;
    }
    ir_func_renumber(m->arena, f);
    return true;
}

static bool reorder_volatile(IrModule *m, const OptConfig *cfg)
{
    IrBlock *b = &m->funcs[0].blocks[0];
    IrInst *first = b->first;
    IrInst *second = first->next;

    (void)cfg;
    first->next = second->next;
    second->next = first;
    b->first = second;
    return true;
}

static void child_dishonest_pass(void)
{
    Arena arena;
    IrModule *m;
    OptConfig cfg;
    static const Pass pass = {"dishonest-test-pass", dishonest_mutation,
                              PASS_PINNED_EXACT};
    static const Pass *const passes[] = {&pass};

    arena_init(&arena);
    m = minimal_module(&arena);
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    (void)opt_run_pass_sequence(m, &cfg, passes, 1);
    arena_free_all(&arena);
}

static void child_oscillating_pass(void)
{
    Arena arena;
    IrModule *m;
    OptConfig cfg;
    static const Pass pass = {"always-true-test-pass", never_converges,
                              PASS_PINNED_EXACT};
    static const Pass *const passes[] = {&pass};

    arena_init(&arena);
    m = minimal_module(&arena);
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    (void)opt_run_fixpoint(m, &cfg, passes, 1, 10);
    arena_free_all(&arena);
}

static void child_structural_oscillator(void)
{
    Arena arena;
    IrModule *m;
    IrFunc *f;
    IrBuilder b;
    BlockId entry;
    IrOperand zero = ir_op_iconst(IRT_I32, 0);
    IrOperand one = ir_op_iconst(IRT_I32, 1);
    OptConfig cfg;
    static const Pass pass = {"structural-oscillator-test-pass",
                              oscillate_structural_size, PASS_PINNED_EXACT};
    static const Pass *const passes[] = {&pass};

    arena_init(&arena);
    m = ir_module_new(&arena, diag_ctx_new(&arena));
    f = ir_func_new(m, "f", IRT_I32, NULL, 0);
    entry = ir_block_new(m, f, "entry");
    ir_builder_at(&b, m, f, entry);
    (void)ir_build2(&b, IR_IADD, IRT_I32, zero, one);
    ir_build_ret(&b, &zero);
    structural_oscillator_inst = NULL;
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    (void)opt_run_fixpoint(m, &cfg, passes, 1, 2);
    arena_free_all(&arena);
}

static void child_volatile_reorder(void)
{
    Arena arena;
    DiagCtx *dc;
    IrModule *m;
    IrFunc *f;
    IrBuilder b;
    BlockId entry;
    IrOperand ptr;
    IrGlobal *g;
    OptConfig cfg;
    static const Pass pass = {"volatile-reorder-test-pass", reorder_volatile,
                              PASS_PINNED_EXACT};
    static const Pass *const passes[] = {&pass};

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    m = ir_module_new(&arena, dc);
    g = ir_global_new(m, "g");
    g->size = 4;
    g->align = 4;
    f = ir_func_new(m, "f", IRT_VOID, NULL, 0);
    entry = ir_block_new(m, f, "entry");
    ir_builder_at(&b, m, f, entry);
    ptr = ir_op_symbol(IRT_PTR, ir_sym(m, "g"), 0);
    ir_build_store(&b, ir_op_iconst(IRT_I32, 1), ptr, 4, IRF_VOLATILE);
    ir_build_store(&b, ir_op_iconst(IRT_I32, 2), ptr, 4, IRF_VOLATILE);
    ir_build_ret(&b, NULL);
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    (void)opt_run_pass_sequence(m, &cfg, passes, 1);
    arena_free_all(&arena);
}

static void child_inline_policy_reorder(void)
{
    Arena arena;
    DiagCtx *dc;
    IrModule *m;
    IrFunc *f;
    IrBuilder b;
    BlockId entry;
    IrOperand ptr;
    IrGlobal *g;
    OptConfig cfg;
    static const Pass pass = {"inline-policy-reorder-test-pass",
                              reorder_volatile, PASS_PINNED_INLINE_CLONES};
    static const Pass *const passes[] = {&pass};

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    m = ir_module_new(&arena, dc);
    g = ir_global_new(m, "g");
    g->size = 4;
    g->align = 4;
    f = ir_func_new(m, "f", IRT_VOID, NULL, 0);
    entry = ir_block_new(m, f, "entry");
    ir_builder_at(&b, m, f, entry);
    ptr = ir_op_symbol(IRT_PTR, ir_sym(m, "g"), 0);
    ir_build_store(&b, ir_op_iconst(IRT_I32, 1), ptr, 4, IRF_VOLATILE);
    ir_build_store(&b, ir_op_iconst(IRT_I32, 2), ptr, 4, IRF_VOLATILE);
    ir_build_ret(&b, NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    (void)opt_run_pass_sequence(m, &cfg, passes, 1);
    arena_free_all(&arena);
}

static int run_child(ChildAction action, char *err, size_t err_cap)
{
    int fd[2];
    pid_t pid;
    int status = 0;
    size_t used = 0;

    if (pipe(fd) != 0)
        return -1;
    /* Avoid children flushing the unit runner's inherited stdout buffer on
     * the intentional cgf_ice()->exit(4) path. */
    fflush(NULL);
    pid = fork();
    if (pid == 0) {
        close(fd[0]);
        if (dup2(fd[1], STDERR_FILENO) < 0)
            _exit(99);
        close(fd[1]);
        action();
        _exit(0);
    }
    close(fd[1]);
    if (pid < 0) {
        close(fd[0]);
        return -1;
    }
    while (used + 1 < err_cap) {
        ssize_t n = read(fd[0], err + used, err_cap - used - 1);

        if (n > 0) {
            used += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
    err[used] = '\0';
    close(fd[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    return status;
}

void test_opt_pass_rejects_false_return_after_mutation(TestCtx *t)
{
    char err[1024];
    int status = run_child(child_dishonest_pass, err, sizeof(err));

    T_ASSERT(t, status >= 0 && WIFEXITED(status));
    if (status >= 0 && WIFEXITED(status))
        T_ASSERT_EQ_INT(t, WEXITSTATUS(status), 4);
    T_ASSERT(t, strstr(err, "dishonest-test-pass") != NULL);
    T_ASSERT(t, strstr(err, "changed-flag mismatch") != NULL);
}

void test_opt_fixpoint_cap_names_still_changing_pass(TestCtx *t)
{
    char err[1024];
    int status = run_child(child_oscillating_pass, err, sizeof(err));

    T_ASSERT(t, status >= 0 && WIFEXITED(status));
    if (status >= 0 && WIFEXITED(status))
        T_ASSERT_EQ_INT(t, WEXITSTATUS(status), 4);
    T_ASSERT(t,
             strstr(err, "opt: fixpoint did not converge after 10 iterations "
                         "without structural progress; still changing: "
                         "always-true-test-pass") != NULL);
}

void test_opt_fixpoint_allows_strict_progress_past_stall_cap(TestCtx *t)
{
    Arena arena;
    IrModule *m;
    IrFunc *f;
    IrBuilder b;
    BlockId entry;
    IrOperand zero = ir_op_iconst(IRT_I32, 0);
    IrOperand one = ir_op_iconst(IRT_I32, 1);
    OptConfig cfg;
    static const Pass pass = {"shrinking-test-pass", shrink_one_instruction,
                              PASS_PINNED_EXACT};
    static const Pass *const passes[] = {&pass};
    u32 i;

    arena_init(&arena);
    m = ir_module_new(&arena, diag_ctx_new(&arena));
    f = ir_func_new(m, "f", IRT_I32, NULL, 0);
    entry = ir_block_new(m, f, "entry");
    ir_builder_at(&b, m, f, entry);
    for (i = 0; i < 12; i++)
        (void)ir_build2(&b, IR_IADD, IRT_I32, zero, one);
    ir_build_ret(&b, &zero);
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, opt_run_fixpoint(m, &cfg, passes, 1, 3));
    T_ASSERT_EQ_INT(t, f->blocks[0].ninsts, 1);
    T_ASSERT_EQ_INT(t, f->blocks[0].last->op, IR_RET);
    T_ASSERT(t, ir_verify(m->dc, m));
    arena_free_all(&arena);
}

void test_opt_fixpoint_structural_oscillation_does_not_reset_cap(TestCtx *t)
{
    char err[1024];
    int status = run_child(child_structural_oscillator, err, sizeof(err));

    T_ASSERT(t, status >= 0 && WIFEXITED(status));
    if (status >= 0 && WIFEXITED(status))
        T_ASSERT_EQ_INT(t, WEXITSTATUS(status), 4);
    T_ASSERT(t, strstr(err, "opt: fixpoint did not converge after 2 iterations "
                            "without structural progress; still changing: "
                            "structural-oscillator-test-pass") != NULL);
}

void test_opt_pass_rejects_volatile_reordering(TestCtx *t)
{
    char err[1024];
    int status;

    status = run_child(child_volatile_reorder, err, sizeof(err));

    T_ASSERT(t, status >= 0 && WIFEXITED(status));
    if (status >= 0 && WIFEXITED(status))
        T_ASSERT_EQ_INT(t, WEXITSTATUS(status), 4);
    T_ASSERT(t, strstr(err, "volatile-reorder-test-pass") != NULL);
    T_ASSERT(t, strstr(err, "changed pinned operations") != NULL);

    status = run_child(child_inline_policy_reorder, err, sizeof(err));
    T_ASSERT(t, status >= 0 && WIFEXITED(status));
    if (status >= 0 && WIFEXITED(status))
        T_ASSERT_EQ_INT(t, WEXITSTATUS(status), 4);
    T_ASSERT(t, strstr(err, "inline-policy-reorder-test-pass") != NULL);
    T_ASSERT(t, strstr(err, "changed pinned operations") != NULL);
}

void test_opt_time_report_is_stderr_only_and_ir_stable(TestCtx *t)
{
    Arena arena;
    IrModule *m;
    OptConfig cfg;
    Buf before, after;
    FILE *report;
    char text[512];
    size_t n;

    arena_init(&arena);
    m = minimal_module(&arena);
    buf_init(&before);
    buf_init(&after);
    ir_print_module_buf(&before, m);
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    if (report) {
        opt_config_init(&cfg, OPT_O1);
        cfg.time_report = true;
        cfg.report = report;
        T_ASSERT(t, !opt_run_pipeline(m, &cfg));
        ir_print_module_buf(&after, m);
        T_ASSERT_EQ_INT(t, (int)before.len, (int)after.len);
        T_ASSERT(t, before.len == after.len &&
                        memcmp(before.data, after.data, before.len) == 0);
        fflush(report);
        rewind(report);
        n = fread(text, 1, sizeof(text) - 1, report);
        text[n] = '\0';
        T_ASSERT(t, strstr(text, "optimization time report:") != NULL);
        T_ASSERT(t, strstr(text, "mem2reg") != NULL);
        T_ASSERT(t, strstr(text, "invocations") != NULL);
        fclose(report);
    }
    buf_free(&before);
    buf_free(&after);
    arena_free_all(&arena);
}
