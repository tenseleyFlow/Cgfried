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
             strstr(err, "opt: fixpoint did not converge after 10 iterations; "
                         "still changing: always-true-test-pass") != NULL);
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
