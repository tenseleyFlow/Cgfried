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

void test_memsafe_lifetime_proven_null_access_matrix(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "func i32 @read() {\n"
                           "entry():\n"
                           "    %p = bitcast i64 0 to ptr\n"
                           "    %v = load i32, %p, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "}\n"
                           "func void @write() {\n"
                           "entry():\n"
                           "    %p = bitcast i64 0 to ptr\n"
                           "    store i32 1, %p, align 4, etype i32\n"
                           "    ret\n"
                           "}\n"
                           "func void @call() {\n"
                           "entry():\n"
                           "    %p = bitcast i64 0 to ptr\n"
                           "    call void %p()\n"
                           "    ret\n"
                           "}\n"
                           "func i32 @derived() {\n"
                           "entry():\n"
                           "    %p = bitcast i64 0 to ptr\n"
                           "    %q = ptradd %p, 0\n"
                           "    %v = load i32, %q, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "}\n"
                           "func i32 @null_branch(ptr %p) {\n"
                           "entry():\n"
                           "    %isnull = icmp eq ptr %p, 0\n"
                           "    condbr %isnull, bad(), good()\n"
                           "bad():\n"
                           "    %v = load i32, %p, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "good():\n"
                           "    ret i32 0\n"
                           "}\n"
                           "func i32 @nonnull_guard(ptr %p) {\n"
                           "entry():\n"
                           "    %nonnull = icmp ne ptr %p, 0\n"
                           "    condbr %nonnull, good(), null()\n"
                           "good():\n"
                           "    %v = load i32, %p, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "null():\n"
                           "    ret i32 0\n"
                           "}\n"
                           "func i32 @offset_null_test(ptr %p) {\n"
                           "entry():\n"
                           "    %q = ptradd %p, 4\n"
                           "    %isnull = icmp eq ptr %q, 0\n"
                           "    condbr %isnull, bad(), good()\n"
                           "bad():\n"
                           "    %v = load i32, %p, align 4, etype i32\n"
                           "    ret i32 %v\n"
                           "good():\n"
                           "    ret i32 0\n"
                           "}\n"
                           "func void @zero_size() {\n"
                           "entry():\n"
                           "    %p = bitcast i64 0 to ptr\n"
                           "    memcpy %p, %p, 0, align 1\n"
                           "    ret\n"
                           "}\n");
    u32 i;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    T_ASSERT_EQ_INT(t, module->nfuncs, 8);
    for (i = 0; i < module->nfuncs; i++) {
        MsFunctionResult *result =
            ms_analyze_function(&fix.arena, module, &module->funcs[i], false);
        u32 expected = i < 5 ? 1 : 0;

        T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE),
                        expected);
        ms_result_free(result);
    }
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_null_and_zero_proofs_after_path_degrade(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(
        &fix,
        "sym @fread\n"
        "sym @malloc\n"
        "sym @reallocarray\n"
        "sym @strncpy\n"
        "sym @bcopy\n"
        "func i32 @literal(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) {\n"
        "entry():\n"
        "    %ca = icmp eq i32 %a, 0\n"
        "    condbr %ca, b(), out()\n"
        "b():\n"
        "    %cb = icmp eq i32 %b, 0\n"
        "    condbr %cb, c(), out()\n"
        "c():\n"
        "    %cc = icmp eq i32 %c, 0\n"
        "    condbr %cc, d(), out()\n"
        "d():\n"
        "    %cd = icmp eq i32 %d, 0\n"
        "    condbr %cd, e(), out()\n"
        "e():\n"
        "    %ce = icmp eq i32 %e, 0\n"
        "    condbr %ce, bad(), out()\n"
        "bad():\n"
        "    %p = bitcast i64 0 to ptr\n"
        "    %q = ptradd %p, 4\n"
        "    %v = load i32, %q, align 4, etype i32\n"
        "    ret i32 %v\n"
        "out():\n"
        "    ret i32 0\n"
        "}\n"
        "func void @literal_call(i32 %a) {\n"
        "entry():\n"
        "    switch i32 %a, bad(), 0: out(), 1: out(), 2: out(), 3: out(), 4: "
        "out()\n"
        "bad():\n"
        "    %p = bitcast i64 0 to ptr\n"
        "    %q = ptradd %p, 4\n"
        "    call void %q()\n"
        "    ret\n"
        "out():\n"
        "    ret\n"
        "}\n"
        "func i32 @predicate(ptr %p, i32 %a, i32 %b, i32 %c, i32 %d) {\n"
        "entry():\n"
        "    %cp = icmp eq ptr %p, 0\n"
        "    condbr %cp, a(), out()\n"
        "a():\n"
        "    %ca = icmp eq i32 %a, 0\n"
        "    condbr %ca, b(), out()\n"
        "b():\n"
        "    %cb = icmp eq i32 %b, 0\n"
        "    condbr %cb, c(), out()\n"
        "c():\n"
        "    %cc = icmp eq i32 %c, 0\n"
        "    condbr %cc, d(), out()\n"
        "d():\n"
        "    %cd = icmp eq i32 %d, 0\n"
        "    condbr %cd, lost(), out()\n"
        "lost():\n"
        "    %v = load i32, %p, align 4, etype i32\n"
        "    call void %p()\n"
        "    ret i32 %v\n"
        "out():\n"
        "    ret i32 0\n"
        "}\n"
        "func void @first_zero(i64 %size, i64 %count, ptr %stream) {\n"
        "entry():\n"
        "    %z = icmp eq i64 %size, 0\n"
        "    condbr %z, zero(), out()\n"
        "zero():\n"
        "    %p = bitcast i64 0 to ptr\n"
        "    %r = call i64 @fread(ptr %p, i64 %size, i64 %count, ptr %stream)\n"
        "    ret\n"
        "out():\n"
        "    ret\n"
        "}\n"
        "func void @second_zero(i64 %size, i64 %count, ptr %stream) {\n"
        "entry():\n"
        "    %nz = icmp ne i64 %count, 0\n"
        "    condbr %nz, out(), zero()\n"
        "zero():\n"
        "    %p = bitcast i64 0 to ptr\n"
        "    %r = call i64 @fread(ptr %p, i64 %size, i64 %count, ptr %stream)\n"
        "    ret\n"
        "out():\n"
        "    ret\n"
        "}\n"
        "func void @ir_memcpy_zero(i64 %size) {\n"
        "entry():\n"
        "    %z = icmp eq i64 %size, 0\n"
        "    condbr %z, zero(), out()\n"
        "zero():\n"
        "    %p = bitcast i64 0 to ptr\n"
        "    memcpy %p, %p, %size, align 1\n"
        "    ret\n"
        "out():\n"
        "    ret\n"
        "}\n"
        "func void @ir_memset_zero(i64 %size) {\n"
        "entry():\n"
        "    %nz = icmp ne i64 %size, 0\n"
        "    condbr %nz, out(), zero()\n"
        "zero():\n"
        "    %p = bitcast i64 0 to ptr\n"
        "    memset %p, 0, %size, align 1\n"
        "    ret\n"
        "out():\n"
        "    ret\n"
        "}\n"
        "func void @first_extent_zero(i64 %size, i64 %count) {\n"
        "entry():\n"
        "    %p = call ptr @malloc(i64 8)\n"
        "    %z = icmp eq i64 %size, 0\n"
        "    condbr %z, zero(), out()\n"
        "zero():\n"
        "    %q = call ptr @reallocarray(ptr %p, i64 %size, i64 %count)\n"
        "    ret\n"
        "out():\n"
        "    ret\n"
        "}\n"
        "func void @second_extent_zero(i64 %size, i64 %count) {\n"
        "entry():\n"
        "    %p = call ptr @malloc(i64 8)\n"
        "    %nz = icmp ne i64 %count, 0\n"
        "    condbr %nz, out(), zero()\n"
        "zero():\n"
        "    %q = call ptr @reallocarray(ptr %p, i64 %size, i64 %count)\n"
        "    ret\n"
        "out():\n"
        "    ret\n"
        "}\n"
        "func i32 @intermediate_null_access(ptr %p) {\n"
        "entry():\n"
        "    %q = ptradd %p, 4\n"
        "    %isnull = icmp eq ptr %q, 0\n"
        "    condbr %isnull, bad(), out()\n"
        "bad():\n"
        "    %r = ptradd %q, 8\n"
        "    %s = ptradd %r, 12\n"
        "    %v = load i32, %s, align 4, etype i32\n"
        "    ret i32 %v\n"
        "out():\n"
        "    ret i32 0\n"
        "}\n"
        "func void @intermediate_null_call(ptr %p) {\n"
        "entry():\n"
        "    %q = ptradd %p, 4\n"
        "    %isnull = icmp eq ptr %q, 0\n"
        "    condbr %isnull, bad(), out()\n"
        "bad():\n"
        "    %r = ptradd %q, 8\n"
        "    %s = ptradd %r, 12\n"
        "    call void %s()\n"
        "    ret\n"
        "out():\n"
        "    ret\n"
        "}\n"
        "func void @strncpy_source_zero(i64 %size) {\n"
        "entry():\n"
        "    %z = icmp eq i64 %size, 0\n"
        "    condbr %z, zero(), out()\n"
        "zero():\n"
        "    %p = bitcast i64 0 to ptr\n"
        "    %r = call ptr @strncpy(ptr %p, ptr %p, i64 %size)\n"
        "    ret\n"
        "out():\n"
        "    ret\n"
        "}\n"
        "func void @bcopy_source_zero(i64 %size) {\n"
        "entry():\n"
        "    %nz = icmp ne i64 %size, 0\n"
        "    condbr %nz, out(), zero()\n"
        "zero():\n"
        "    %p = bitcast i64 0 to ptr\n"
        "    call void @bcopy(ptr %p, ptr %p, i64 %size)\n"
        "    ret\n"
        "out():\n"
        "    ret\n"
        "}\n"
        "func void @fread_null_stream_zero(i64 %size) {\n"
        "entry():\n"
        "    %z = icmp eq i64 %size, 0\n"
        "    condbr %z, zero(), out()\n"
        "zero():\n"
        "    %p = bitcast i64 0 to ptr\n"
        "    %r = call i64 @fread(ptr %p, i64 %size, i64 8, ptr %p)\n"
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
    T_ASSERT_EQ_INT(t, module->nfuncs, 14);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 1);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[1], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 1);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[2], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 0);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[3], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 0);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[4], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 0);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[5], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 0);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[6], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 0);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[7], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_REALLOC_ZERO), 1);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[8], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_REALLOC_ZERO), 1);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[9], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 1);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[10], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 1);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[11], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 0);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[12], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 0);
    ms_result_free(result);
    result = ms_analyze_function(&fix.arena, module, &module->funcs[13], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_NULL_DEREFERENCE), 1);
    ms_result_free(result);
    arena_free_all(&fix.arena);
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

void test_memsafe_lifetime_null_test_survives_block_parameter(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @malloc\n"
                           "sym @free\n"
                           "func void @f() {\n"
                           "entry():\n"
                           "    %p = call ptr @malloc(i64 8)\n"
                           "    %isnull = icmp eq ptr %p, 0\n"
                           "    br join(i32 %isnull)\n"
                           "join(i32 %condition):\n"
                           "    condbr %condition, failed(), success()\n"
                           "failed():\n"
                           "    ret\n"
                           "success():\n"
                           "    call void @free(ptr %p)\n"
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
    T_ASSERT(t, (a == MS_UNALLOCATED && b == MS_FREED) ||
                    (a == MS_FREED && b == MS_UNALLOCATED));
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_LEAK), 0);
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

void test_memsafe_lifetime_prunes_owned_vs_standard_stream_equality(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(
        &fix, "sym @fopen\n"
              "sym @fclose\n"
              "sym @stdin\n"
              "sym @path\n"
              "sym @mode\n"
              "func void @f(i32 %choose_standard) {\n"
              "entry():\n"
              "    %slot = alloca 8, align 8\n"
              "    store ptr 0, %slot, align 8\n"
              "    %choose = icmp ne i32 %choose_standard, 0\n"
              "    condbr %choose, standard(), owned()\n"
              "standard():\n"
              "    %standard = load ptr, @stdin, align 8\n"
              "    store ptr %standard, %slot, align 8\n"
              "    br join()\n"
              "join():\n"
              "    %p = load ptr, %slot, align 8\n"
              "    %nonnull = icmp ne ptr %p, 0\n"
              "    condbr %nonnull, compare(), condition(i32 0)\n"
              "owned():\n"
              "    %opened = call ptr @fopen(ptr @path, ptr @mode)\n"
              "    store ptr %opened, %slot, align 8\n"
              "    br join()\n"
              "compare():\n"
              "    %candidate = load ptr, %slot, align 8\n"
              "    %stdin_value = load ptr, @stdin, align 8\n"
              "    %different = icmp ne ptr %candidate, %stdin_value\n"
              "    br condition(i32 %different)\n"
              "condition(i32 %should_close):\n"
              "    condbr %should_close, close(), done()\n"
              "close():\n"
              "    %to_close = load ptr, %slot, align 8\n"
              "    %status = call i32 @fclose(ptr %to_close)\n"
              "    br done()\n"
              "done():\n"
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
    T_ASSERT(t, !ms_result_degraded(result));
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_standard_stream_overwrite_does_not_hide_owned_leak(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(
        &fix, "sym @fopen\n"
              "sym @stdin\n"
              "sym @path\n"
              "sym @mode\n"
              "func void @f() {\n"
              "entry():\n"
              "    %opened = call ptr @fopen(ptr @path, ptr @mode)\n"
              "    %standard = load ptr, @stdin, align 8\n"
              "    %different = icmp ne ptr %standard, %standard\n"
              "    condbr %different, impossible(), done()\n"
              "impossible():\n"
              "    ret\n"
              "done():\n"
              "    ret\n"
              "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_LEAK), 1);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_select_may_equal_standard_stream_keeps_owned_leak(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(
        &fix, "sym @fopen\n"
              "sym @fclose\n"
              "sym @stdin\n"
              "sym @path\n"
              "sym @mode\n"
              "func void @f(i32 %choose) {\n"
              "entry():\n"
              "    %owned = call ptr @fopen(ptr @path, ptr @mode)\n"
              "    %standard = load ptr, @stdin, align 8\n"
              "    %candidate = select %choose, ptr %standard, %owned\n"
              "    %equal = icmp eq ptr %candidate, %standard\n"
              "    condbr %equal, skipped(), close()\n"
              "skipped():\n"
              "    ret\n"
              "close():\n"
              "    %status = call i32 @fclose(ptr %candidate)\n"
              "    ret\n"
              "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_LEAK), 1);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_nonpointer_slot_overwrite_forgets_pointer_origin(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(
        &fix, "sym @fopen\n"
              "sym @fclose\n"
              "sym @stdin\n"
              "sym @path\n"
              "sym @mode\n"
              "func void @f() {\n"
              "entry():\n"
              "    %slot = alloca 8, align 8\n"
              "    %owned = call ptr @fopen(ptr @path, ptr @mode)\n"
              "    %nonnull = icmp ne ptr %owned, 0\n"
              "    condbr %nonnull, live(), failure()\n"
              "live():\n"
              "    store ptr %owned, %slot, align 8, etype ptr\n"
              "    %standard = load ptr, @stdin, align 8\n"
              "    %bits = bitcast ptr %standard to i64\n"
              "    store i64 %bits, %slot, align 8, etype i64\n"
              "    %candidate = load ptr, %slot, align 8\n"
              "    %equal = icmp eq ptr %candidate, %standard\n"
              "    condbr %equal, leak(), close()\n"
              "leak():\n"
              "    ret\n"
              "close():\n"
              "    %status = call i32 @fclose(ptr %owned)\n"
              "    ret\n"
              "failure():\n"
              "    ret\n"
              "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_LEAK), 1);
    T_ASSERT(t, !ms_result_degraded(result));
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_pointer_origin_saturation_stays_conservative(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(
        &fix, "sym @fopen\n"
              "sym @fclose\n"
              "sym @stdin\n"
              "sym @path\n"
              "sym @mode\n"
              "func void @f() {\n"
              "entry():\n"
              "    %slot = alloca 8, align 8\n"
              "    %d0 = alloca 8, align 8\n"
              "    %d1 = alloca 8, align 8\n"
              "    %d2 = alloca 8, align 8\n"
              "    %d3 = alloca 8, align 8\n"
              "    %d4 = alloca 8, align 8\n"
              "    %d5 = alloca 8, align 8\n"
              "    %d6 = alloca 8, align 8\n"
              "    %d7 = alloca 8, align 8\n"
              "    %owned = call ptr @fopen(ptr @path, ptr @mode)\n"
              "    %nonnull = icmp ne ptr %owned, 0\n"
              "    condbr %nonnull, live(), failure()\n"
              "live():\n"
              "    store ptr %owned, %slot, align 8, etype ptr\n"
              "    %standard = load ptr, @stdin, align 8\n"
              "    %bits = bitcast ptr %standard to i64\n"
              "    store i64 %bits, %slot, align 8, etype i64\n"
              "    store ptr %standard, %d0, align 8, etype ptr\n"
              "    store ptr %standard, %d1, align 8, etype ptr\n"
              "    store ptr %standard, %d2, align 8, etype ptr\n"
              "    store ptr %standard, %d3, align 8, etype ptr\n"
              "    store ptr %standard, %d4, align 8, etype ptr\n"
              "    store ptr %standard, %d5, align 8, etype ptr\n"
              "    store ptr %standard, %d6, align 8, etype ptr\n"
              "    store ptr %standard, %d7, align 8, etype ptr\n"
              "    %candidate = load ptr, %slot, align 8\n"
              "    %equal = icmp eq ptr %candidate, %standard\n"
              "    condbr %equal, leak(), close()\n"
              "leak():\n"
              "    ret\n"
              "close():\n"
              "    %status = call i32 @fclose(ptr %owned)\n"
              "    ret\n"
              "failure():\n"
              "    ret\n"
              "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_LEAK), 1);
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

void test_memsafe_lifetime_index_guard_does_not_hide_leak(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @malloc\n"
                           "sym @cgf_safe_check_index\n"
                           "func void @f(i64 %index) {\n"
                           "entry():\n"
                           "    %p = call ptr @malloc(i64 8)\n"
                           "    %off = imul i64 %index, 4\n"
                           "    %q = ptradd %p, %off\n"
                           "    call void @cgf_safe_check_index(ptr %p, i64 "
                           "%index, i64 4, i32 0, ptr %q, i32 1)\n"
                           "    ret\n"
                           "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_LEAK), 1);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_index_guard_preserves_reload_uaf(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @malloc\n"
                           "sym @free\n"
                           "sym @cgf_safe_check_index\n"
                           "func i8 @f(i64 %index) {\n"
                           "entry():\n"
                           "    %slot = alloca 8, align 8\n"
                           "    %p = call ptr @malloc(i64 16)\n"
                           "    store ptr %p, %slot, align 8, etype ptr\n"
                           "    %off = imul i64 %index, 1\n"
                           "    %q = ptradd %slot, %off\n"
                           "    call void @cgf_safe_check_index(ptr %slot, i64 "
                           "%index, i64 1, i32 0, ptr %q, i32 1)\n"
                           "    %reload = load ptr, %slot, align 8, etype ptr\n"
                           "    call void @free(ptr %p)\n"
                           "    %value = load i8, %reload, align 1, etype i8\n"
                           "    ret i8 %value\n"
                           "}\n");
    MsFunctionResult *result;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_USE_AFTER_FREE), 1);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_lifetime_malformed_index_guard_still_escapes(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(&fix, "sym @malloc\n"
                                          "sym @cgf_safe_check_index\n"
                                          "func void @f() {\n"
                                          "entry():\n"
                                          "    %p = call ptr @malloc(i64 8)\n"
                                          "    call void "
                                          "@cgf_safe_check_index(ptr %p)\n"
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

void test_memsafe_freopen_standard_stream_stays_escaped(TestCtx *t)
{
    MsFix fix;
    IrModule *module =
        parse_module(&fix, "sym @freopen\n"
                           "sym @stderr\n"
                           "sym @path\n"
                           "sym @mode\n"
                           "func void @f() {\n"
                           "entry():\n"
                           "    %standard = load ptr, @stderr, align 8\n"
                           "    %replacement = call ptr @freopen(ptr @path, "
                           "ptr @mode, ptr %standard)\n"
                           "    %nonnull = icmp ne ptr %replacement, 0\n"
                           "    condbr %nonnull, success(), failure()\n"
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
    T_ASSERT_EQ_INT(t, issue_count_kind(result, MS_ISSUE_LEAK), 0);
    for (i = 0; i < ms_result_exit_count(result); i++) {
        MsState replacement = ms_result_exit_state(result, i, 0);

        saw_success |= replacement == MS_ESCAPED;
        saw_failure |= replacement == MS_UNALLOCATED;
    }
    T_ASSERT(t, saw_success);
    T_ASSERT(t, saw_failure);
    ms_result_free(result);
    arena_free_all(&fix.arena);
}

void test_memsafe_freopen_owned_stream_stays_locally_owned(TestCtx *t)
{
    MsFix fix;
    IrModule *module = parse_module(
        &fix, "sym @fopen\n"
              "sym @freopen\n"
              "sym @path\n"
              "sym @mode\n"
              "func void @f() {\n"
              "entry():\n"
              "    %stream = call ptr @fopen(ptr @path, ptr @mode)\n"
              "    %replacement = call ptr @freopen(ptr @path, "
              "ptr @mode, ptr %stream)\n"
              "    %nonnull = icmp ne ptr %replacement, 0\n"
              "    condbr %nonnull, success(), failure()\n"
              "success():\n"
              "    ret\n"
              "failure():\n"
              "    ret\n"
              "}\n");
    MsFunctionResult *result;
    u32 i;
    bool saw_owned_success = false;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    result = ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    for (i = 0; i < ms_result_exit_count(result); i++)
        saw_owned_success |= ms_result_exit_state(result, i, 1) == MS_ALLOCATED;
    T_ASSERT(t, saw_owned_success);
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
