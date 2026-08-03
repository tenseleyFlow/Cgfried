#include <string.h>

#include "memsafe/memsafe.h"
#include "unit.h"
#include "util/buf.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} MsFix;

static void silent_sink(void *user, const Diag *diag, const DiagCtx *dc)
{
    (void)user;
    (void)diag;
    (void)dc;
}

static IrModule *parse_module(MsFix *fix, const char *source)
{
    DiagSink sink = {silent_sink, NULL};
    IrModule *module;

    arena_init(&fix->arena);
    fix->dc = diag_ctx_new(&fix->arena);
    diag_set_sink(fix->dc, sink);
    module = ir_parse_module(&fix->arena, fix->dc, source, "<memsafe-test>");
    if (module && !ir_verify(fix->dc, module))
        return NULL;
    return module;
}

static u32 issue_count_kind(const MsFunctionResult *result, MsIssueKind kind)
{
    u32 count = 0;
    u32 i;

    for (i = 0; i < ms_result_issue_count(result); i++) {
        const MsIssue *issue = ms_result_issue_at(result, i);

        if (issue && issue->kind == kind)
            count++;
    }
    return count;
}

void test_memsafe_lifetime_malloc_free(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "sym @free\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    call void @free(ptr %p)\n"
                                          "    ret\n"
                                          "}\n");
    MsFunctionResult *result;
    const MsTrace *trace;
    MsEvent event;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 1);
    T_ASSERT_EQ_INT(t, ms_result_exit_state(result, 0, 0), MS_FREED);
    trace = ms_result_exit_trace(result, 0, 0);
    T_ASSERT(t, trace != NULL);
    T_ASSERT_EQ_INT(t, trace ? trace->len : 0, 2);
    T_ASSERT(t, trace && ms_trace_event(trace, 0, &event));
    T_ASSERT_EQ_INT(t, event.kind, MS_EV_ALLOC);
    T_ASSERT(t, trace && ms_trace_event(trace, 1, &event));
    T_ASSERT_EQ_INT(t, event.kind, MS_EV_FREE);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_null_branch_keeps_two_paths(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "sym @free\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    %c = icmp ne ptr %p, 0\n"
                                          "    condbr %c, yes(), join()\n"
                                          "yes():\n"
                                          "    call void @free(ptr %p)\n"
                                          "    br join()\n"
                                          "join():\n"
                                          "    ret\n"
                                          "}\n");
    MsFunctionResult *result;
    MsState a, b;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_block_state_count(result, 2), 2);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 2);
    a = ms_result_exit_state(result, 0, 0);
    b = ms_result_exit_state(result, 1, 0);
    T_ASSERT(t, (a == MS_UNALLOCATED && b == MS_FREED) ||
                    (a == MS_FREED && b == MS_UNALLOCATED));
    T_ASSERT_EQ_INT(t, ms_result_split_count(result), 1);
    T_ASSERT(t, !ms_result_degraded(result));
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_unknown_call_escapes(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "sym @consume\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    call void @consume(ptr %p)\n"
                                          "    ret\n"
                                          "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_exit_state(result, 0, 0), MS_ESCAPED);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_unknown_call_escapes_captured_site(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "sym @consume\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    %box = alloca 8, align 8\n"
                                          "    store ptr %p, %box, align 8, "
                                          "etype ptr\n"
                                          "    call void @consume(ptr %box)\n"
                                          "    ret\n"
                                          "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_exit_state(result, 0, 0), MS_ESCAPED);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_unreachable_is_not_an_exit(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    unreachable\n"
                                          "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 0);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_state_cap_joins_excess_paths(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(
        &fix, "sym @malloc\n"
              "func void @within(i32 %a, i32 %b, i32 %c) {\n"
              "entry():\n"
              "    condbr %a, ayes(), btest()\n"
              "ayes():\n"
              "    %pa = call ptr @malloc(i64 1)\n"
              "    br btest()\n"
              "btest():\n"
              "    condbr %b, byes(), ctest()\n"
              "byes():\n"
              "    %pb = call ptr @malloc(i64 2)\n"
              "    br ctest()\n"
              "ctest():\n"
              "    condbr %c, cyes(), join()\n"
              "cyes():\n"
              "    %pc = call ptr @malloc(i64 3)\n"
              "    br join()\n"
              "join():\n"
              "    ret\n"
              "}\n"
              "func void @overflow(i32 %a, i32 %b, i32 %c, i32 %d) {\n"
              "entry():\n"
              "    condbr %a, ayes(), btest()\n"
              "ayes():\n"
              "    %pa = call ptr @malloc(i64 1)\n"
              "    br btest()\n"
              "btest():\n"
              "    condbr %b, byes(), ctest()\n"
              "byes():\n"
              "    %pb = call ptr @malloc(i64 2)\n"
              "    br ctest()\n"
              "ctest():\n"
              "    condbr %c, cyes(), dtest()\n"
              "cyes():\n"
              "    %pc = call ptr @malloc(i64 3)\n"
              "    br dtest()\n"
              "dtest():\n"
              "    condbr %d, dyes(), join()\n"
              "dyes():\n"
              "    %pd = call ptr @malloc(i64 4)\n"
              "    br join()\n"
              "join():\n"
              "    ret\n"
              "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_split_count(result), 7);
    T_ASSERT_EQ_INT(t, ms_result_block_state_count(result, 6), 8);
    T_ASSERT(t, !ms_result_degraded(result));
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[1], false);
    T_ASSERT_EQ_INT(t, ms_result_split_count(result), 15);
    T_ASSERT_EQ_INT(t, ms_result_block_state_count(result, 8), 8);
    T_ASSERT(t, ms_result_degraded(result));
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_prunes_conflicting_equalities(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "func void @f(i32 %x) {\n"
                           "entry():\n"
                           "    %one = icmp eq i32 %x, 1\n"
                           "    condbr %one, next(), out0()\n"
                           "next():\n"
                           "    %two = icmp eq i32 %x, 2\n"
                           "    condbr %two, impossible(), out1()\n"
                           "impossible():\n"
                           "    ret\n"
                           "out0():\n"
                           "    ret\n"
                           "out1():\n"
                           "    ret\n"
                           "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_block_state_count(result, 2), 0);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 2);
    T_ASSERT(t, !ms_result_degraded(result));
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_switch_default_excludes_cases(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "func void @f(i32 %x) {\n"
                           "entry():\n"
                           "    switch i32 %x, def(), 1: hit()\n"
                           "def():\n"
                           "    %same = icmp eq i32 %x, 1\n"
                           "    condbr %same, impossible(), out()\n"
                           "impossible():\n"
                           "    ret\n"
                           "hit():\n"
                           "    ret\n"
                           "out():\n"
                           "    ret\n"
                           "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_block_state_count(result, 2), 0);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 2);
    T_ASSERT(t, !ms_result_degraded(result));
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_nonnull_branch_preserves_freed_state(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "sym @free\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    call void @free(ptr %p)\n"
                                          "    %nonnull = icmp ne ptr %p, 0\n"
                                          "    condbr %nonnull, yes(), no()\n"
                                          "yes():\n"
                                          "    ret\n"
                                          "no():\n"
                                          "    ret\n"
                                          "}\n");
    MsFunctionResult *result;
    MsState a, b;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 2);
    a = ms_result_exit_state(result, 0, 0);
    b = ms_result_exit_state(result, 1, 0);
    T_ASSERT(t, (a == MS_FREED && b == MS_UNALLOCATED) ||
                    (a == MS_UNALLOCATED && b == MS_FREED));
    T_ASSERT(t, a != MS_ALLOCATED && b != MS_ALLOCATED);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_free_disambiguates_path_specific_site(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "sym @free\n"
                                          "func void @f(i32 %pick) {\n"
                                          "entry():\n"
                                          "    condbr %pick, left(), right()\n"
                                          "left():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    br join(ptr %p)\n"
                                          "right():\n"
                                          "    %q = call ptr @malloc(i64 16)\n"
                                          "    br join(ptr %q)\n"
                                          "join(ptr %selected):\n"
                                          "    call void @free(ptr %selected)\n"
                                          "    ret\n"
                                          "}\n");
    MsFunctionResult *result;
    u32 i;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 2);
    for (i = 0; i < 2; i++) {
        MsState a = ms_result_exit_state(result, i, 0);
        MsState b = ms_result_exit_state(result, i, 1);

        T_ASSERT(t, (a == MS_FREED && b == MS_UNALLOCATED) ||
                        (a == MS_UNALLOCATED && b == MS_FREED));
    }
    T_ASSERT(t, ms_result_exit_state(result, 0, 0) !=
                    ms_result_exit_state(result, 1, 0));
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_large_switch_saturates_split_budget(TestCtx *t)
{
    MsFix fix;
    Buf source;
    IrModule *module;
    MsFunctionResult *result;
    u32 i;

    buf_init(&source);
    buf_printf(&source, "sym @malloc\nfunc void @f(i32 %%x) {\nentry():\n"
                        "    %%p = call ptr @malloc(i64 8)\n"
                        "    switch i32 %%x, join()");
    for (i = 0; i < MS_MAX_SPLITS_PER_FUNCTION + 1; i++)
        buf_printf(&source, ", %u: join()", i);
    buf_printf(&source, "\njoin():\n    ret\n}\n");
    buf_push_u8(&source, 0);
    module = parse_module(&fix, (const char *)source.data);
    T_ASSERT(t, module != NULL);
    if (!module) {
        buf_free(&source);
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_split_count(result),
                    MS_MAX_SPLITS_PER_FUNCTION);
    T_ASSERT_EQ_INT(t, ms_result_block_state_count(result, 1), 1);
    T_ASSERT(t, ms_result_degraded(result));
    ms_result_free(result);
    buf_free(&source);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_predicate_cap_drops_oldest_term(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(
        &fix, "func void @within(i32 %a, i32 %b, i32 %c, i32 %d) {\n"
              "entry():\n"
              "    %ca = icmp eq i32 %a, 0\n"
              "    condbr %ca, b1(), done0()\n"
              "b1():\n"
              "    %cb = icmp eq i32 %b, 0\n"
              "    condbr %cb, b2(), done1()\n"
              "b2():\n"
              "    %cc = icmp eq i32 %c, 0\n"
              "    condbr %cc, b3(), done2()\n"
              "b3():\n"
              "    %cd = icmp eq i32 %d, 0\n"
              "    condbr %cd, done3(), done4()\n"
              "done0():\n"
              "    ret\n"
              "done1():\n"
              "    ret\n"
              "done2():\n"
              "    ret\n"
              "done3():\n"
              "    ret\n"
              "done4():\n"
              "    ret\n"
              "}\n"
              "func void @overflow(i32 %a, i32 %b, i32 %c, i32 %d, "
              "i32 %e) {\n"
              "entry():\n"
              "    %ca = icmp eq i32 %a, 0\n"
              "    condbr %ca, b1(), done0()\n"
              "b1():\n"
              "    %cb = icmp eq i32 %b, 0\n"
              "    condbr %cb, b2(), done1()\n"
              "b2():\n"
              "    %cc = icmp eq i32 %c, 0\n"
              "    condbr %cc, b3(), done2()\n"
              "b3():\n"
              "    %cd = icmp eq i32 %d, 0\n"
              "    condbr %cd, b4(), done3()\n"
              "b4():\n"
              "    %ce = icmp eq i32 %e, 0\n"
              "    condbr %ce, done4(), done5()\n"
              "done0():\n"
              "    ret\n"
              "done1():\n"
              "    ret\n"
              "done2():\n"
              "    ret\n"
              "done3():\n"
              "    ret\n"
              "done4():\n"
              "    ret\n"
              "done5():\n"
              "    ret\n"
              "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_split_count(result), 4);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 5);
    T_ASSERT(t, !ms_result_degraded(result));
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[1], false);
    T_ASSERT_EQ_INT(t, ms_result_split_count(result), 5);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 6);
    T_ASSERT(t, ms_result_degraded(result));
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_reports_direct_use_after_free_with_trace(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @malloc\n"
                           "sym @free\n"
                           "func i32 @f() {\n"
                           "entry():\n"
                           "    %p = call ptr @malloc(i64 8)\n"
                           "    call void @free(ptr %p)\n"
                           "    %v = load i32, %p, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "}\n");
    MsFunctionResult *result;
    const MsIssue *issue;
    MsEvent event;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_USE_AFTER_FREE), 1);
    issue = ms_result_issue_at(result, 0);
    T_ASSERT(t, issue != NULL);
    T_ASSERT_EQ_INT(t, issue ? issue->kind : MS_ISSUE_COUNT,
                    MS_ISSUE_USE_AFTER_FREE);
    T_ASSERT_EQ_INT(t, issue ? issue->site_id : 0, 1);
    T_ASSERT(t, issue && !issue->strict);
    T_ASSERT_EQ_INT(t, issue ? issue->trace.len : 0, 2);
    T_ASSERT(t, issue && ms_trace_event(&issue->trace, 0, &event));
    T_ASSERT_EQ_INT(t, event.kind, MS_EV_ALLOC);
    T_ASSERT(t, issue && ms_trace_event(&issue->trace, 1, &event));
    T_ASSERT_EQ_INT(t, event.kind, MS_EV_FREE);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_reports_double_free_once(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "sym @free\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    call void @free(ptr %p)\n"
                                          "    call void @free(ptr %p)\n"
                                          "    ret\n"
                                          "}\n");
    MsFunctionResult *result;
    const MsIssue *issue;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_DOUBLE_FREE), 1);
    issue = ms_result_issue_at(result, 0);
    T_ASSERT_EQ_INT(t, issue ? issue->kind : MS_ISSUE_COUNT,
                    MS_ISSUE_DOUBLE_FREE);
    T_ASSERT_EQ_INT(t, issue ? issue->site_id : 0, 1);
    T_ASSERT_EQ_INT(t, issue ? issue->trace.len : 0, 2);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_reports_leak_at_return(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    ret\n"
                                          "}\n");
    MsFunctionResult *result;
    const MsIssue *issue;
    MsEvent event;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_issue_count(result), 1);
    issue = ms_result_issue_at(result, 0);
    T_ASSERT_EQ_INT(t, issue ? issue->kind : MS_ISSUE_COUNT, MS_ISSUE_LEAK);
    T_ASSERT_EQ_INT(t, issue ? issue->trace.len : 0, 2);
    T_ASSERT(t, issue && ms_trace_event(&issue->trace, 1, &event));
    T_ASSERT_EQ_INT(t, event.kind, MS_EV_RETURN);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_escape_suppresses_leak_issue(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "sym @consume\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    call void @consume(ptr %p)\n"
                                          "    ret\n"
                                          "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_LEAK), 0);
    T_ASSERT_EQ_INT(t, ms_result_issue_count(result), 0);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_noreturn_call_suppresses_leak_issue(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "sym @abort\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    call void @abort() noreturn\n"
                                          "    unreachable\n"
                                          "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 0);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_LEAK), 0);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_reports_constant_out_of_bounds_read(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @malloc\n"
                           "func i32 @f() {\n"
                           "entry():\n"
                           "    %p = call ptr @malloc(i64 8)\n"
                           "    %past = ptradd %p, 8\n"
                           "    %v = load i32, %past, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_OUT_OF_BOUNDS), 1);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_UNINIT_READ), 0);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_reports_uninitialized_malloc_read(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @malloc\n"
                           "func i32 @f() {\n"
                           "entry():\n"
                           "    %p = call ptr @malloc(i64 8)\n"
                           "    %v = load i32, %p, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_UNINIT_READ), 1);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_initialized_write_suppresses_uninit_read(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @malloc\n"
                           "func i32 @f() {\n"
                           "entry():\n"
                           "    %p = call ptr @malloc(i64 8)\n"
                           "    store i32 7, %p, align 4, etype i32\n"
                           "    %v = load i32, %p, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_UNINIT_READ), 0);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_calloc_read_is_initialized(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @calloc\n"
                           "func i32 @f() {\n"
                           "entry():\n"
                           "    %p = call ptr @calloc(i64 2, i64 4)\n"
                           "    %v = load i32, %p, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_UNINIT_READ), 0);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_OUT_OF_BOUNDS), 0);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_deduplicates_same_issue_across_paths(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @malloc\n"
                           "sym @free\n"
                           "func i32 @f(i32 %choose) {\n"
                           "entry():\n"
                           "    %p = call ptr @malloc(i64 8)\n"
                           "    call void @free(ptr %p)\n"
                           "    condbr %choose, join(), join()\n"
                           "join():\n"
                           "    %v = load i32, %p, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_USE_AFTER_FREE), 1);
    T_ASSERT_EQ_INT(t, ms_result_issue_count(result), 1);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_realloc_correlates_old_and_new_sites(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @malloc\n"
                           "sym @realloc\n"
                           "func void @f() {\n"
                           "entry():\n"
                           "    %p = call ptr @malloc(i64 8)\n"
                           "    %q = call ptr @realloc(ptr %p, i64 16)\n"
                           "    %ok = icmp ne ptr %q, 0\n"
                           "    condbr %ok, success(), failure()\n"
                           "success():\n"
                           "    ret\n"
                           "failure():\n"
                           "    ret\n"
                           "}\n");
    MsFunctionResult *result;
    u32 i;
    bool saw_success = false;
    bool saw_failure = false;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_exit_count(result), 2);
    for (i = 0; i < ms_result_exit_count(result); i++) {
        MsState old = ms_result_exit_state(result, i, 0);
        MsState replacement = ms_result_exit_state(result, i, 1);

        if (old == MS_FREED && replacement == MS_ALLOCATED)
            saw_success = true;
        if (old == MS_ALLOCATED && replacement == MS_UNALLOCATED)
            saw_failure = true;
    }
    T_ASSERT(t, saw_success);
    T_ASSERT(t, saw_failure);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_equal_span_issue_order_is_deterministic(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @malloc\n"
                           "sym @free\n"
                           "func i32 @f() {\n"
                           "entry():\n"
                           "    %p = call ptr @malloc(i64 8)\n"
                           "    call void @free(ptr %p)\n"
                           "    %v = load i32, %p, align 4, etype i32\n"
                           "    call void @free(ptr %p)\n"
                           "    ret i32 %v\n"
                           "}\n");
    MsFunctionResult *result;
    MsFunctionResult *again;
    const MsIssue *first;
    const MsIssue *second;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_issue_count(result), 2);
    first = ms_result_issue_at(result, 0);
    second = ms_result_issue_at(result, 1);
    T_ASSERT_EQ_INT(t, first ? first->kind : MS_ISSUE_COUNT,
                    MS_ISSUE_USE_AFTER_FREE);
    T_ASSERT_EQ_INT(t, second ? second->kind : MS_ISSUE_COUNT,
                    MS_ISSUE_DOUBLE_FREE);
    again = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_issue_count(again), 2);
    T_ASSERT_EQ_INT(t, ms_result_issue_at(again, 0)->kind,
                    MS_ISSUE_USE_AFTER_FREE);
    T_ASSERT_EQ_INT(t, ms_result_issue_at(again, 1)->kind,
                    MS_ISSUE_DOUBLE_FREE);
    ms_result_free(again);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}
