#include <string.h>

#include "memsafe/memsafe.h"
#include "opt/opt.h"
#include "unit.h"
#include "warn/warn.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} SummaryFix;

static void summary_silent_sink(void *user, const Diag *diag, const DiagCtx *dc)
{
    (void)user;
    (void)diag;
    (void)dc;
}

static void summary_count_sink(void *user, const Diag *diag, const DiagCtx *dc)
{
    u32 *count = user;

    (void)diag;
    (void)dc;
    (*count)++;
}

static IrModule *summary_parse(SummaryFix *fix, const char *source)
{
    DiagSink sink = {summary_silent_sink, NULL};
    IrModule *module;

    arena_init(&fix->arena);
    fix->dc = diag_ctx_new(&fix->arena);
    diag_set_sink(fix->dc, sink);
    module = ir_parse_module(&fix->arena, fix->dc, source, "<summary-test>");
    if (module && !ir_verify(fix->dc, module))
        return NULL;
    return module;
}

static u32 safe_check_count(const IrModule *module)
{
    u32 count = 0;
    u32 fi;

    for (fi = 0; fi < module->nfuncs; fi++) {
        const IrFunc *function = &module->funcs[fi];
        u32 bi;

        for (bi = 0; bi < function->nblocks; bi++) {
            const IrInst *in;

            for (in = function->blocks[bi].first; in; in = in->next)
                if (in->op == IR_CALL && in->callee < module->nsyms &&
                    strcmp(module->syms[in->callee], "cgf_safe_check") == 0)
                    count++;
        }
    }
    return count;
}

static void expect_check_stats(TestCtx *t, const IrModule *module,
                               const MsCheckStats *stats)
{
    T_ASSERT_EQ_INT(t, stats->total, stats->discharged + stats->emitted);
    T_ASSERT_EQ_INT(t, safe_check_count(module), stats->emitted);
}

void test_memsafe_state_join_exhaustive(TestCtx *t)
{
    MsState a, b;

    for (a = MS_UNALLOCATED; a < MS_STATE_COUNT; a++)
        for (b = MS_UNALLOCATED; b < MS_STATE_COUNT; b++)
            T_ASSERT_EQ_INT(t, ms_state_join(a, b), a == b ? a : MS_UNKNOWN);
}

static void expect_transition(TestCtx *t, MsState from, MsAction action,
                              bool is_null, MsState state, MsOutcome outcome)
{
    MsTransition got = ms_transition(from, action, is_null);

    T_ASSERT_EQ_INT(t, got.state, state);
    T_ASSERT_EQ_INT(t, got.outcome, outcome);
}

void test_memsafe_state_transitions(TestCtx *t)
{
    static const MsState free_states[MS_STATE_COUNT] = {
        MS_UNALLOCATED, MS_FREED, MS_FREED, MS_UNKNOWN, MS_UNKNOWN,
    };
    static const MsOutcome free_outcomes[MS_STATE_COUNT] = {
        MS_OUTCOME_OK, MS_OUTCOME_OK, MS_OUTCOME_DOUBLE_FREE,
        MS_OUTCOME_OK, MS_OUTCOME_OK,
    };
    static const MsState escape_states[MS_STATE_COUNT] = {
        MS_UNALLOCATED, MS_ESCAPED, MS_FREED, MS_ESCAPED, MS_UNKNOWN,
    };
    static const MsOutcome escape_outcomes[MS_STATE_COUNT] = {
        MS_OUTCOME_OK, MS_OUTCOME_OK, MS_OUTCOME_UAF_ESCAPE,
        MS_OUTCOME_OK, MS_OUTCOME_OK,
    };
    MsState state;

    for (state = MS_UNALLOCATED; state < MS_STATE_COUNT; state++)
        expect_transition(t, state, MS_ACT_ALLOC, false, MS_ALLOCATED,
                          MS_OUTCOME_OK);
    for (state = MS_UNALLOCATED; state < MS_STATE_COUNT; state++) {
        expect_transition(t, state, MS_ACT_FREE, false, free_states[state],
                          free_outcomes[state]);
        expect_transition(t, state, MS_ACT_ESCAPE, false, escape_states[state],
                          escape_outcomes[state]);
        expect_transition(t, state, MS_ACT_DEREF, false, state,
                          state == MS_FREED ? MS_OUTCOME_UAF : MS_OUTCOME_OK);
        expect_transition(t, state, MS_ACT_FREE, true, state, MS_OUTCOME_NOOP);
    }
}

void test_memsafe_trace_persistent_order_and_interning(TestCtx *t)
{
    Arena arena;
    MsTrace trace, split;
    MsEvent first, second, sibling, repeated;
    Span a = {.file_id = 1, .line = 2, .col = 3};
    Span b = {.file_id = 1, .line = 4, .col = 5};

    arena_init(&arena);
    ms_trace_init(&trace, &arena);
    ms_trace_push(&trace, a, MS_EV_ALLOC, "allocated %s", "here");
    split = trace;
    ms_trace_push(&trace, b, MS_EV_FREE, "freed here");
    ms_trace_push(&split, b, MS_EV_ESCAPE, "escaped here");
    ms_trace_push(&trace, b, MS_EV_USE, "allocated %s", "here");
    T_ASSERT_EQ_INT(t, trace.len, 3);
    T_ASSERT_EQ_INT(t, split.len, 2);
    T_ASSERT(t, ms_trace_event(&trace, 0, &first));
    T_ASSERT(t, ms_trace_event(&trace, 1, &second));
    T_ASSERT(t, ms_trace_event(&trace, 2, &repeated));
    T_ASSERT(t, ms_trace_event(&split, 1, &sibling));
    T_ASSERT_EQ_INT(t, first.kind, MS_EV_ALLOC);
    T_ASSERT_EQ_INT(t, second.kind, MS_EV_FREE);
    T_ASSERT_EQ_INT(t, sibling.kind, MS_EV_ESCAPE);
    T_ASSERT_EQ_INT(t, repeated.kind, MS_EV_USE);
    T_ASSERT_EQ_INT(t, second.loc.line, 4);
    T_ASSERT_EQ_INT(t, second.loc.col, 5);
    T_ASSERT_EQ_INT(t, second.loc.file_id, 1);
    T_ASSERT_EQ_STR(t, first.note, "allocated here");
    T_ASSERT_EQ_STR(t, second.note, "freed here");
    T_ASSERT_EQ_STR(t, sibling.note, "escaped here");
    T_ASSERT(t, first.note == repeated.note);
    T_ASSERT(t, !ms_trace_event(&trace, 3, &first));
    arena_free_all(&arena);
}

void test_memsafe_allocation_family_table(TestCtx *t)
{
    static const MsAllocFamily expected[] = {
        {"malloc", true, MS_NO_ARG, MS_NO_ARG, false, true, false, false,
         MS_ALLOC_SUCCESS_DIRECT, 0, MS_NO_ARG, false},
        {"calloc", true, MS_NO_ARG, MS_NO_ARG, false, true, true, true,
         MS_ALLOC_SUCCESS_DIRECT, 0, 1, false},
        {"realloc", true, MS_NO_ARG, 0, true, true, false, false,
         MS_ALLOC_SUCCESS_DIRECT, 1, MS_NO_ARG, false},
        {"reallocarray", true, MS_NO_ARG, 0, true, true, false, false,
         MS_ALLOC_SUCCESS_DIRECT, 1, 2, false},
        {"free", false, MS_NO_ARG, 0, false, false, false, false,
         MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, false},
        {"strdup", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
         MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, false},
        {"strndup", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
         MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, false},
        {"asprintf", true, 0, MS_NO_ARG, false, true, false, true,
         MS_ALLOC_SUCCESS_STATUS_NONNEG, MS_NO_ARG, MS_NO_ARG, false},
        {"vasprintf", true, 0, MS_NO_ARG, false, true, false, true,
         MS_ALLOC_SUCCESS_STATUS_NONNEG, MS_NO_ARG, MS_NO_ARG, false},
        {"aligned_alloc", true, MS_NO_ARG, MS_NO_ARG, false, true, false, false,
         MS_ALLOC_SUCCESS_DIRECT, 1, MS_NO_ARG, false},
        {"posix_memalign", true, 0, MS_NO_ARG, false, true, false, false,
         MS_ALLOC_SUCCESS_STATUS_ZERO, 2, MS_NO_ARG, false},
        {"fopen", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
         MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
        {"fdopen", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
         MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
        {"tmpfile", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
         MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
        {"popen", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true,
         MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
        {"freopen", true, MS_NO_ARG, 2, true, true, false, true,
         MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
        {"fclose", false, MS_NO_ARG, 0, false, false, false, false,
         MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
        {"pclose", false, MS_NO_ARG, 0, false, false, false, false,
         MS_ALLOC_SUCCESS_DIRECT, MS_NO_ARG, MS_NO_ARG, true},
    };
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(expected); i++) {
        const MsAllocFamily *family = ms_alloc_family_lookup(expected[i].name);

        T_ASSERT(t, family != NULL);
        if (!family)
            continue;
        T_ASSERT_EQ_STR(t, family->name, expected[i].name);
        T_ASSERT_EQ_INT(t, family->allocates, expected[i].allocates);
        T_ASSERT_EQ_INT(t, family->alloc_out_arg, expected[i].alloc_out_arg);
        T_ASSERT_EQ_INT(t, family->frees_arg, expected[i].frees_arg);
        T_ASSERT_EQ_INT(t, family->frees_on_success,
                        expected[i].frees_on_success);
        T_ASSERT_EQ_INT(t, family->returns_ownership,
                        expected[i].returns_ownership);
        T_ASSERT_EQ_INT(t, family->zeroes, expected[i].zeroes);
        T_ASSERT_EQ_INT(t, family->fully_written, expected[i].fully_written);
        T_ASSERT_EQ_INT(t, family->success, expected[i].success);
        T_ASSERT_EQ_INT(t, family->size_arg, expected[i].size_arg);
        T_ASSERT_EQ_INT(t, family->size_arg2, expected[i].size_arg2);
        T_ASSERT_EQ_INT(t, family->is_file_resource,
                        expected[i].is_file_resource);
    }
    T_ASSERT(t, ms_alloc_family_lookup("not_an_allocator") == NULL);
    T_ASSERT(t, ms_alloc_family_lookup(NULL) == NULL);
    T_ASSERT_EQ_INT(t, ms_alloc_family_lookup("posix_memalign")->alloc_out_arg,
                    0);
}

void test_memsafe_alias_seed_translation(TestCtx *t)
{
    Arena arena;
    IrModule module = {0};
    IrFunc function = {0};
    IrBlock block = {0};
    IrInst malloc_call = {0}, free_call = {0}, posix_call = {0};
    IrInst mismatch_call = {0};
    /* A row is selected by shape as well as by name, so these synthetic
     * calls must carry the operands a real one does: a bare {0} call has no
     * arguments and no result, which is not what any of these look like. */
    IrOperand malloc_args[1] = {{IROP_ICONST, IRT_I64, 0, 0, 8, 0}};
    IrOperand free_args[1] = {{IROP_VALUE, IRT_PTR, 0, 0, 1, 0}};
    IrOperand posix_args[3] = {{IROP_VALUE, IRT_PTR, 0, 0, 2, 0},
                               {IROP_ICONST, IRT_I64, 0, 0, 16, 0},
                               {IROP_ICONST, IRT_I64, 0, 0, 32, 0}};
    const char *syms[] = {"malloc", "free", "posix_memalign"};
    AliasAllocSeed seed, *seeds = NULL;

    arena_init(&arena);
    module.syms = syms;
    module.nsyms = CGF_ARRAY_LEN(syms);
    malloc_call.op = IR_CALL;
    malloc_call.subop = FUNCREF_EXTERNAL;
    malloc_call.callee = 0;
    malloc_call.result.v = 3;
    malloc_call.type = IRT_PTR;
    malloc_call.nops = 1;
    malloc_call.ops = malloc_args;
    malloc_call.next = &free_call;
    free_call.op = IR_CALL;
    free_call.subop = FUNCREF_EXTERNAL;
    free_call.callee = 1;
    free_call.nops = 1;
    free_call.ops = free_args;
    free_call.next = &posix_call;
    posix_call.op = IR_CALL;
    posix_call.subop = FUNCREF_EXTERNAL;
    posix_call.callee = 2;
    posix_call.result.v = 4;
    posix_call.type = IRT_I32;
    posix_call.nops = 3;
    posix_call.ops = posix_args;
    block.first = &malloc_call;
    block.last = &posix_call;
    function.blocks = &block;
    function.nblocks = 1;

    T_ASSERT(t, ms_alloc_seed_for_call(&module, &malloc_call, &seed));
    T_ASSERT(t, seed.call == &malloc_call);
    T_ASSERT(t, seed.owns_result);
    T_ASSERT_EQ_INT(t, seed.out_param, ALIAS_NO_OUT_PARAM);
    T_ASSERT(t, !ms_alloc_seed_for_call(&module, &free_call, &seed));
    T_ASSERT(t, ms_alloc_seed_for_call(&module, &posix_call, &seed));
    T_ASSERT(t, seed.call == &posix_call);
    T_ASSERT(t, !seed.owns_result);
    T_ASSERT_EQ_INT(t, seed.out_param, 0);
    T_ASSERT_EQ_INT(t, ms_alias_alloc_seeds(&arena, &module, &function, &seeds),
                    2);
    T_ASSERT(t, seeds != NULL);
    T_ASSERT(t, seeds[0].call == &malloc_call);
    T_ASSERT(t, seeds[1].call == &posix_call);

    /* Same name, incompatible shape: an implicitly declared malloc returns
     * int, so it is not the allocator the row describes and must not become
     * an allocation site. Attaching the row anyway handed the alias service
     * a pointer fact about an integer result, which it refused as an ICE.
     * Frontend fuzzer, seed 64271. */
    mismatch_call.op = IR_CALL;
    mismatch_call.subop = FUNCREF_EXTERNAL;
    mismatch_call.callee = 0;
    mismatch_call.result.v = 5;
    mismatch_call.type = IRT_I32;
    mismatch_call.nops = 1;
    mismatch_call.ops = malloc_args;
    T_ASSERT(t, ms_alloc_family_lookup("malloc") != NULL);
    T_ASSERT(t, ms_alloc_family_for_call("malloc", &mismatch_call) == NULL);
    T_ASSERT(t, ms_alloc_family_for_call("malloc", &malloc_call) != NULL);
    T_ASSERT(t, !ms_alloc_seed_for_call(&module, &mismatch_call, &seed));
    /* strcpy's return_alias needs a pointer result and pointer arguments. */
    T_ASSERT(t, ms_lib_summary_lookup("strcpy") != NULL);
    T_ASSERT(t, ms_lib_summary_for_call("strcpy", &mismatch_call) == NULL);
    arena_free_all(&arena);
}

void test_memsafe_lib_summary_table_covers_reviewed_libc_surface(TestCtx *t)
{
    static const char *const names[] = {
        "memcpy",  "memmove", "memset",   "memcmp",     "memchr",      "strlen",
        "strnlen", "strcmp",  "strncmp",  "strcasecmp", "strncasecmp", "strcpy",
        "strncpy", "strcat",  "strncat",  "strchr",     "strrchr",     "strstr",
        "strpbrk", "strspn",  "strcspn",  "strtok",     "strerror_r",  "bcopy",
        "bzero",   "fread",   "fwrite",   "fgets",      "fputs",       "puts",
        "printf",  "fprintf", "snprintf", "sprintf",    "scanf",       "sscanf",
        "fscanf",  "getenv",  "getcwd",   "realpath",   "read",        "write",
        "pread",   "pwrite",  "qsort",    "bsearch",    "free",        "fopen",
        "fdopen",  "tmpfile", "popen",    "freopen",    "fflush",      "fclose",
        "pclose",
    };
    u32 i;

    T_ASSERT(t, CGF_ARRAY_LEN(names) >= 40);
    for (i = 0; i < CGF_ARRAY_LEN(names); i++) {
        const MsLibSummary *summary = ms_lib_summary_lookup(names[i]);

        T_ASSERT(t, summary != NULL);
        if (summary)
            T_ASSERT_EQ_STR(t, summary->name, names[i]);
    }
    T_ASSERT(t, ms_lib_summary_lookup("not_a_libc_function") == NULL);
    T_ASSERT(t, ms_lib_summary_lookup(NULL) == NULL);
}

void test_memsafe_lib_summary_records_alias_and_write_effects(TestCtx *t)
{
    const MsLibSummary *memcpy_summary = ms_lib_summary_lookup("memcpy");
    const MsLibSummary *strchr_summary = ms_lib_summary_lookup("strchr");

    T_ASSERT(t, memcpy_summary != NULL);
    T_ASSERT(t, strchr_summary != NULL);
    if (!memcpy_summary || !strchr_summary)
        return;
    T_ASSERT_EQ_INT(t, memcpy_summary->deref_mask, 3);
    T_ASSERT_EQ_INT(t, memcpy_summary->write_mask, 1);
    T_ASSERT_EQ_INT(t, memcpy_summary->return_alias, 0);
    T_ASSERT_EQ_INT(t, memcpy_summary->write_size_arg, 2);
    T_ASSERT_EQ_INT(t, memcpy_summary->write_size_arg2, -1);
    T_ASSERT_EQ_INT(t, strchr_summary->deref_mask, 1);
    T_ASSERT_EQ_INT(t, strchr_summary->write_mask, 0);
    T_ASSERT_EQ_INT(t, strchr_summary->return_alias, 0);
    T_ASSERT_EQ_INT(t, strchr_summary->write_size_arg, -1);
}

void test_memsafe_lib_summary_records_memory_and_file_release_pairs(TestCtx *t)
{
    const MsLibSummary *free_summary = ms_lib_summary_lookup("free");
    const MsLibSummary *fclose_summary = ms_lib_summary_lookup("fclose");

    T_ASSERT(t, free_summary != NULL);
    T_ASSERT(t, fclose_summary != NULL);
    if (!free_summary || !fclose_summary)
        return;
    T_ASSERT_EQ_INT(t, free_summary->free_mask, 1);
    T_ASSERT_EQ_INT(t, fclose_summary->free_mask, 1);
    T_ASSERT_EQ_INT(t, fclose_summary->deref_mask, 1);
    T_ASSERT(t, !free_summary->returns_ownership);
    T_ASSERT(t, !fclose_summary->returns_ownership);
}

void test_memsafe_lib_summary_marks_variadic_portions_conservative(TestCtx *t)
{
    const MsLibSummary *printf_summary = ms_lib_summary_lookup("printf");
    const MsLibSummary *fprintf_summary = ms_lib_summary_lookup("fprintf");
    const MsLibSummary *snprintf_summary = ms_lib_summary_lookup("snprintf");
    const MsLibSummary *memcpy_summary = ms_lib_summary_lookup("memcpy");

    T_ASSERT_EQ_INT(t, ms_lib_summary_variadic_from(printf_summary), 1);
    T_ASSERT_EQ_INT(t, ms_lib_summary_variadic_from(fprintf_summary), 2);
    T_ASSERT_EQ_INT(t, ms_lib_summary_variadic_from(snprintf_summary), 3);
    T_ASSERT_EQ_INT(t, ms_lib_summary_variadic_from(memcpy_summary), (u32)-1);
    T_ASSERT_EQ_INT(t, ms_lib_summary_variadic_from(NULL), (u32)-1);
}

void test_memsafe_summary_propagates_nonzero_libc_argument_masks(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "sym @fread\n"
                            "sym @freopen\n"
                            "sym @strerror_r\n"
                            "func void @helper(ptr %buffer, ptr %path, "
                            "ptr %mode, ptr %stream, ptr %scratch) {\n"
                            "entry():\n"
                            "    %n = call i64 @fread(ptr %buffer, i64 1, "
                            "i64 2, ptr %stream)\n"
                            "    %replacement = call ptr @freopen(ptr %path, "
                            "ptr %mode, ptr %stream)\n"
                            "    %status = call i32 @strerror_r(i32 1, "
                            "ptr %scratch, i64 4)\n"
                            "    ret\n"
                            "}\n");
    MsSummarySet *set;
    const MsSummary *helper;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    set = ms_summary_build(&fix.arena, module, false, NULL);
    helper = ms_summary_get(set, 0);
    T_ASSERT(t, helper != NULL);
    if (helper) {
        T_ASSERT(t, helper->params[0].dereferenced);
        T_ASSERT(t, helper->params[0].written);
        T_ASSERT(t, helper->params[1].dereferenced);
        T_ASSERT(t, helper->params[2].dereferenced);
        T_ASSERT(t, helper->params[3].dereferenced);
        T_ASSERT(t, helper->params[3].may_free);
        T_ASSERT(t, helper->params[4].dereferenced);
        T_ASSERT(t, helper->params[4].written);
    }
    arena_free_all(&fix.arena);
}

void test_memsafe_summary_diamond_propagates_bottom_up(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "sym @free\n"
                            "func void @leaf(ptr %p) internal {\n"
                            "entry():\n"
                            "    call void @free(ptr %p)\n"
                            "    ret\n"
                            "}\n"
                            "func void @left(ptr %p) internal {\n"
                            "entry():\n"
                            "    call void @leaf(ptr %p)\n"
                            "    ret\n"
                            "}\n"
                            "func void @right(ptr %p) internal {\n"
                            "entry():\n"
                            "    call void @leaf(ptr %p)\n"
                            "    ret\n"
                            "}\n"
                            "func void @root(ptr %p) {\n"
                            "entry():\n"
                            "    call void @left(ptr %p)\n"
                            "    call void @right(ptr %p)\n"
                            "    ret\n"
                            "}\n");
    MsSummarySet *set;
    const MsSummary *left;
    const MsSummary *right;
    const MsSummary *root;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    set = ms_summary_build(&fix.arena, module, false, NULL);
    left = ms_summary_get(set, 1);
    right = ms_summary_get(set, 2);
    root = ms_summary_get(set, 3);
    T_ASSERT(t, left && !left->top && left->params[0].may_free);
    T_ASSERT(t, right && !right->top && right->params[0].may_free);
    T_ASSERT(t, root && !root->top && root->params[0].may_free);
    arena_free_all(&fix.arena);
}

void test_memsafe_summary_forwards_mixed_owned_and_alias_return(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module = summary_parse(
        &fix, "sym @malloc\n"
              "func ptr @mixed(ptr %p, i32 %fresh) internal {\n"
              "entry():\n"
              "    condbr %fresh, take_owned(), take_alias()\n"
              "take_owned():\n"
              "    %owned = call ptr @malloc(i64 8)\n"
              "    ret ptr %owned\n"
              "take_alias():\n"
              "    ret ptr %p\n"
              "}\n"
              "func ptr @forward(ptr %p, i32 %fresh) {\n"
              "entry():\n"
              "    %result = call ptr @mixed(ptr %p, i32 %fresh)\n"
              "    ret ptr %result\n"
              "}\n"
              "func ptr @arena_load(ptr %p) {\n"
              "entry():\n"
              "    %slot = alloca 8, align 8\n"
              "    %unrelated = call ptr @malloc(i64 8)\n"
              "    store ptr %unrelated, %slot, align 8, etype ptr\n"
              "    store ptr %p, %slot, align 8, etype ptr\n"
              "    %result = load ptr, %slot, align 8, etype ptr\n"
              "    ret ptr %result\n"
              "}\n");
    MsSummarySet *set;
    const MsSummary *mixed;
    const MsSummary *forward;
    const MsSummary *arena_load;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    set = ms_summary_build(&fix.arena, module, false, NULL);
    mixed = ms_summary_get(set, 0);
    forward = ms_summary_get(set, 1);
    arena_load = ms_summary_get(set, 2);
    T_ASSERT(t, mixed && mixed->returns_ownership);
    T_ASSERT(t, mixed && !mixed->must_return_ownership);
    T_ASSERT(t, mixed && mixed->params[0].returned_alias);
    T_ASSERT(t, forward && forward->returns_ownership);
    T_ASSERT(t, forward && !forward->must_return_ownership);
    T_ASSERT(t, forward && forward->params[0].returned_alias);
    T_ASSERT(t, arena_load && !arena_load->returns_ownership);
    T_ASSERT(t, arena_load && arena_load->params[0].returned_alias);
    arena_free_all(&fix.arena);
}

void test_memsafe_summary_marks_recursive_scc_as_top(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "func void @left(ptr %p) internal {\n"
                            "entry():\n"
                            "    call void @right(ptr %p)\n"
                            "    ret\n"
                            "}\n"
                            "func void @right(ptr %p) internal {\n"
                            "entry():\n"
                            "    call void @left(ptr %p)\n"
                            "    ret\n"
                            "}\n");
    MsSummarySet *set;
    const MsSummary *left;
    const MsSummary *right;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    set = ms_summary_build(&fix.arena, module, false, NULL);
    left = ms_summary_get(set, 0);
    right = ms_summary_get(set, 1);
    T_ASSERT(t, left && left->top);
    T_ASSERT(t, right && right->top);
    arena_free_all(&fix.arena);
}

void test_memsafe_summary_recursive_top_does_not_prove_owned_return(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "sym @malloc\n"
                            "func ptr @recursive(i32 %again) internal {\n"
                            "entry():\n"
                            "    condbr %again, recurse(), allocate()\n"
                            "recurse():\n"
                            "    %next = call ptr @recursive(i32 %again)\n"
                            "    ret ptr %next\n"
                            "allocate():\n"
                            "    %fresh = call ptr @malloc(i64 8)\n"
                            "    ret ptr %fresh\n"
                            "}\n"
                            "func ptr @wrapper(i32 %again) {\n"
                            "entry():\n"
                            "    %result = call ptr @recursive(i32 %again)\n"
                            "    ret ptr %result\n"
                            "}\n");
    MsSummarySet *set;
    const MsSummary *recursive;
    const MsSummary *wrapper;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    set = ms_summary_build(&fix.arena, module, false, NULL);
    recursive = ms_summary_get(set, 0);
    wrapper = ms_summary_get(set, 1);
    T_ASSERT(t, recursive && recursive->top);
    T_ASSERT(t, recursive && recursive->returns_ownership);
    T_ASSERT(t, wrapper && !wrapper->top);
    T_ASSERT(t, wrapper && !wrapper->returns_ownership);
    arena_free_all(&fix.arena);
}

void test_memsafe_summary_borrowed_return_override(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "sym @malloc\n"
                            "func ptr @fresh(ptr %p) internal {\n"
                            "entry():\n"
                            "    %q = call ptr @malloc(i64 8)\n"
                            "    ret ptr %q\n"
                            "}\n"
                            "func ptr @caller(ptr %p) {\n"
                            "entry():\n"
                            "    %q = call ptr @fresh(ptr %p)\n"
                            "    ret ptr %q\n"
                            "}\n");
    CgfAttr borrowed = {CGF_ATTR_RETURNS_BORROWED, 1, 1, {0}, NULL};
    MsSummarySet *set;
    const MsSummary *fresh;
    const MsSummary *caller;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    module->funcs[0].cgf_attrs = &borrowed;
    set = ms_summary_build(&fix.arena, module, false, NULL);
    fresh = ms_summary_get(set, 0);
    caller = ms_summary_get(set, 1);
    T_ASSERT(t, fresh && !fresh->returns_ownership);
    T_ASSERT(t, fresh && fresh->params[0].returned_alias);
    T_ASSERT(t, caller && !caller->returns_ownership);
    T_ASSERT(t, caller && caller->params[0].returned_alias);
    arena_free_all(&fix.arena);
}

void test_memsafe_summary_recursive_annotation_wrapper(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "sym @free\n"
                            "func void @recursive(ptr %p) internal {\n"
                            "entry():\n"
                            "    call void @free(ptr %p)\n"
                            "    call void @recursive(ptr %p)\n"
                            "    ret\n"
                            "}\n"
                            "func void @wrapper(ptr %p) {\n"
                            "entry():\n"
                            "    call void @recursive(ptr %p)\n"
                            "    ret\n"
                            "}\n");
    CgfAttr borrows = {CGF_ATTR_BORROWS, 1, 1, {0}, NULL};
    MsSummarySet *set;
    const MsSummary *recursive;
    const MsSummary *wrapper;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    module->funcs[0].cgf_attrs = &borrows;
    set = ms_summary_build(&fix.arena, module, false, NULL);
    recursive = ms_summary_get(set, 0);
    wrapper = ms_summary_get(set, 1);
    T_ASSERT(t, recursive && recursive->top);
    T_ASSERT(t, recursive && recursive->params[0].annot_borrow);
    T_ASSERT(t, recursive && !recursive->params[0].may_free);
    T_ASSERT(t, recursive && !recursive->params[0].escapes);
    T_ASSERT(t, wrapper && !wrapper->top);
    T_ASSERT(t, wrapper && !wrapper->params[0].may_free);
    T_ASSERT(t, wrapper && !wrapper->params[0].escapes);
    arena_free_all(&fix.arena);
}

void test_memsafe_summary_owned_return_suppresses_alias(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "func ptr @owned(ptr %p) internal {\n"
                            "entry():\n"
                            "    ret ptr %p\n"
                            "}\n"
                            "func ptr @caller(ptr %p) {\n"
                            "entry():\n"
                            "    %q = call ptr @owned(ptr %p)\n"
                            "    ret ptr %q\n"
                            "}\n");
    CgfAttr owned = {CGF_ATTR_RETURNS_OWNED, 0, 0, {0}, NULL};
    MsSummarySet *set;
    const MsSummary *callee;
    const MsSummary *caller;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    module->funcs[0].cgf_attrs = &owned;
    set = ms_summary_build(&fix.arena, module, false, NULL);
    callee = ms_summary_get(set, 0);
    caller = ms_summary_get(set, 1);
    T_ASSERT(t, callee && callee->returns_ownership);
    T_ASSERT(t, callee && callee->params[0].returned_alias);
    T_ASSERT(t, caller && caller->returns_ownership);
    T_ASSERT(t, caller && !caller->params[0].returned_alias);
    arena_free_all(&fix.arena);
}

static void check_annotation_mismatch_suppression(TestCtx *t, bool pragma)
{
    SummaryFix fix;
    IrModule *module = summary_parse(&fix, "sym @malloc\n"
                                           "func ptr @suppressed(ptr %p) {\n"
                                           "entry():\n"
                                           "    %q = call ptr @malloc(i64 8)\n"
                                           "    ret ptr %q\n"
                                           "}\n");
    CgfAttr borrowed = {CGF_ATTR_RETURNS_BORROWED, 1, 1, {0}, NULL};
    DiagSink sink;
    WarnCtx *warnings;
    u32 count = 0;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    module->funcs[0].cgf_attrs = &borrowed;
    sink.handle = summary_count_sink;
    sink.user = &count;
    diag_set_sink(fix.dc, sink);
    warnings = warn_ctx_new(&fix.arena, fix.dc);
    if (pragma)
        warn_pragma_set(warnings, 0, WARN_MEM_ANNOTATION_MISMATCH,
                        WARN_PRAGMA_IGNORED);
    else
        T_ASSERT(t, warn_flag(warnings, "no-mem-annotation-mismatch"));
    (void)ms_summary_build(&fix.arena, module, false, warnings);
    T_ASSERT_EQ_INT(t, count, 0);
    arena_free_all(&fix.arena);
}

void test_memsafe_summary_suppressed_mismatch_has_no_orphan_notes(TestCtx *t)
{
    check_annotation_mismatch_suppression(t, false);
    check_annotation_mismatch_suppression(t, true);
}

void test_memsafe_summary_may_free_vs_annotated_must(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module = summary_parse(
        &fix, "sym @free\n"
              "func void @inferred(ptr %p) internal {\n"
              "entry():\n"
              "    call void @free(ptr %p)\n"
              "    ret\n"
              "}\n"
              "func void @contract(ptr %p) {\n"
              "entry():\n"
              "    ret\n"
              "}\n"
              "func void @conditional(ptr %p, i32 %take) internal {\n"
              "entry():\n"
              "    condbr %take, consume(), keep()\n"
              "consume():\n"
              "    call void @free(ptr %p)\n"
              "    ret\n"
              "keep():\n"
              "    ret\n"
              "}\n");
    CgfAttr takes = {CGF_ATTR_TAKES_OWNERSHIP, 1, 1, {0}, NULL};
    MsSummarySet *set;
    const MsSummary *inferred;
    const MsSummary *contract;
    const MsSummary *conditional;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    module->funcs[1].cgf_attrs = &takes;
    set = ms_summary_build(&fix.arena, module, false, NULL);
    inferred = ms_summary_get(set, 0);
    contract = ms_summary_get(set, 1);
    conditional = ms_summary_get(set, 2);
    T_ASSERT(t, inferred && inferred->params[0].may_free);
    T_ASSERT(t, inferred && inferred->params[0].must_free);
    T_ASSERT(t, contract && contract->params[0].may_free);
    T_ASSERT(t, contract && contract->params[0].must_free);
    T_ASSERT(t, conditional && conditional->params[0].may_free);
    T_ASSERT(t, conditional && !conditional->params[0].must_free);
    arena_free_all(&fix.arena);
}

void test_memsafe_instrument_discharges_proven_heap_access(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "sym @malloc\n"
                            "sym @free\n"
                            "func i32 @f(i32 %choose) {\n"
                            "entry():\n"
                            "    %p = call ptr @malloc(i64 8)\n"
                            "    %ok = icmp ne ptr %p, 0\n"
                            "    condbr %ok, live(), null()\n"
                            "live():\n"
                            "    condbr %choose, left(), right()\n"
                            "left():\n"
                            "    br join()\n"
                            "right():\n"
                            "    br join()\n"
                            "join():\n"
                            "    %v = load i32, %p, align 4, volatile, etype "
                            "i32\n"
                            "    call void @free(ptr %p)\n"
                            "    ret i32 %v\n"
                            "null():\n"
                            "    ret i32 0\n"
                            "}\n");
    MsCheckStats stats;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    ms_process_module(NULL, module, false, NULL, true, &stats);
    T_ASSERT_EQ_INT(t, stats.total, 1);
    T_ASSERT_EQ_INT(t, stats.discharged, 1);
    T_ASSERT_EQ_INT(t, stats.emitted, 0);
    expect_check_stats(t, module, &stats);
    {
        const IrInst *setter = module->funcs[0].blocks[0].first;

        T_ASSERT(t, setter && setter->op == IR_CALL &&
                        setter->callee < module->nsyms &&
                        strcmp(module->syms[setter->callee],
                               "cgf_safe_set_next_site") == 0);
        T_ASSERT(t, setter && setter->nops == 1 && setter->ops[0].a == 1);
        T_ASSERT(t,
                 setter && setter->next && setter->next->op == IR_CALL &&
                     strcmp(module->syms[setter->next->callee], "malloc") == 0);
    }
    T_ASSERT(t, ir_verify(fix.dc, module));
    arena_free_all(&fix.arena);
}

void test_memsafe_instrument_emits_for_unproven_pointer_access(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "func i32 @f(ptr %p) {\n"
                            "entry():\n"
                            "    %v = load i32, %p, align 4, etype i32\n"
                            "    ret i32 %v\n"
                            "}\n");
    MsCheckStats stats;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    ms_process_module(NULL, module, false, NULL, true, &stats);
    T_ASSERT_EQ_INT(t, stats.total, 1);
    T_ASSERT_EQ_INT(t, stats.discharged, 0);
    T_ASSERT_EQ_INT(t, stats.emitted, 1);
    expect_check_stats(t, module, &stats);
    {
        const IrInst *check = module->funcs[0].blocks[0].first;

        T_ASSERT(t, check && check->nops == 4);
        if (check && check->nops == 4)
            T_ASSERT_EQ_INT(t, check->ops[3].a, 1);
    }
    T_ASSERT(t, ir_verify(fix.dc, module));
    arena_free_all(&fix.arena);
}

void test_memsafe_instrument_checks_pointer_derivation(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "sym @malloc\n"
                            "func i32 @f(i64 %offset) {\n"
                            "entry():\n"
                            "    %p = call ptr @malloc(i64 8)\n"
                            "    %q = ptradd %p, %offset\n"
                            "    %v = load i32, %q, align 4, etype i32\n"
                            "    ret i32 %v\n"
                            "}\n");
    MsCheckStats stats;
    const IrInst *in;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    ms_process_module(NULL, module, false, NULL, true, &stats);
    T_ASSERT_EQ_INT(t, stats.emitted, 1);
    in = module->funcs[0].blocks[0].first;
    while (in &&
           !(in->op == IR_CALL && in->callee < module->nsyms &&
             strcmp(module->syms[in->callee], "cgf_safe_check_derive") == 0))
        in = in->next;
    T_ASSERT(t, in != NULL);
    T_ASSERT(t, in && in->nops == 4);
    T_ASSERT(t, in && in->ops[0].kind == IROP_VALUE && in->ops[0].a == 2);
    T_ASSERT(t, in && in->ops[1].kind == IROP_VALUE && in->ops[1].a == 1);
    T_ASSERT(t, in && in->ops[2].kind == IROP_VALUE && in->ops[2].a == 3);
    T_ASSERT(t, ir_verify(fix.dc, module));
    arena_free_all(&fix.arena);
}

void test_memsafe_instrument_assigns_unique_derivation_sites(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "sym @malloc\n"
                            "func i32 @f(i64 %first, i64 %second) {\n"
                            "entry():\n"
                            "    %base = call ptr @malloc(i64 8)\n"
                            "    %p = ptradd %base, %first\n"
                            "    %q = ptradd %p, %second\n"
                            "    %v = load i32, %q, align 4, etype i32\n"
                            "    ret i32 %v\n"
                            "}\n");
    MsCheckStats stats;
    const IrInst *in;
    u32 sites[2] = {0};
    u32 count = 0;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    ms_process_module(NULL, module, false, NULL, true, &stats);
    for (in = module->funcs[0].blocks[0].first; in; in = in->next)
        if (in->op == IR_CALL && in->callee < module->nsyms &&
            strcmp(module->syms[in->callee], "cgf_safe_check_derive") == 0 &&
            count < 2)
            sites[count++] = in->ops[3].a;
    T_ASSERT_EQ_INT(t, count, 2);
    T_ASSERT(t, sites[0] != sites[1]);
    T_ASSERT(t, ir_verify(fix.dc, module));
    arena_free_all(&fix.arena);
}

void test_memsafe_instrument_checks_uintptr_round_trip(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "func ptr @f(ptr %p) {\n"
                            "entry():\n"
                            "    %bits = bitcast ptr %p to i64\n"
                            "    %adjusted = or i64 %bits, 1\n"
                            "    %q = bitcast i64 %adjusted to ptr\n"
                            "    ret ptr %q\n"
                            "}\n");
    MsCheckStats stats;
    const IrInst *in;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    ms_process_module(NULL, module, false, NULL, true, &stats);
    in = module->funcs[0].blocks[0].first;
    while (in && !(in->op == IR_CALL && in->callee < module->nsyms &&
                   strcmp(module->syms[in->callee],
                          "cgf_safe_check_round_trip") == 0))
        in = in->next;
    T_ASSERT(t, in != NULL);
    T_ASSERT(t, in && in->nops == 3);
    T_ASSERT(t, in && in->ops[0].kind == IROP_VALUE && in->ops[0].a == 1);
    T_ASSERT(t, in && in->ops[1].kind == IROP_VALUE && in->ops[1].a == 4);
    T_ASSERT(t, ir_verify(fix.dc, module));
    arena_free_all(&fix.arena);
}

void test_memsafe_instrument_keeps_statically_diagnosed_oob_access(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "sym @malloc\n"
                            "sym @free\n"
                            "func i32 @f() {\n"
                            "entry():\n"
                            "    %p = call ptr @malloc(i64 8)\n"
                            "    %ok = icmp ne ptr %p, 0\n"
                            "    condbr %ok, live(), null()\n"
                            "live():\n"
                            "    %past = ptradd %p, 8\n"
                            "    %v = load i32, %past, align 4, etype i32\n"
                            "    call void @free(ptr %p)\n"
                            "    ret i32 %v\n"
                            "null():\n"
                            "    ret i32 0\n"
                            "}\n");
    MsFunctionResult *analysis;
    const MsIssue *issue;
    MsCheckStats stats;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    analysis =
        ms_analyze_function(&fix.arena, module, &module->funcs[0], false);
    T_ASSERT_EQ_INT(t, ms_result_issue_count(analysis), 1);
    issue = ms_result_issue_at(analysis, 0);
    T_ASSERT_EQ_INT(t, issue ? issue->kind : MS_ISSUE_COUNT,
                    MS_ISSUE_OUT_OF_BOUNDS);
    ms_result_free(analysis);

    ms_process_module(NULL, module, false, NULL, true, &stats);
    T_ASSERT_EQ_INT(t, stats.total, 1);
    T_ASSERT_EQ_INT(t, stats.discharged, 0);
    T_ASSERT_EQ_INT(t, stats.emitted, 1);
    expect_check_stats(t, module, &stats);
    T_ASSERT(t, ir_verify(fix.dc, module));
    arena_free_all(&fix.arena);
}

void test_memsafe_instrument_discharges_stack_access(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "func i32 @f() {\n"
                            "entry():\n"
                            "    %p = alloca 4, align 4\n"
                            "    %v = load i32, %p, align 4, volatile, etype "
                            "i32\n"
                            "    ret i32 %v\n"
                            "}\n");
    MsCheckStats stats;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    ms_process_module(NULL, module, false, NULL, true, &stats);
    T_ASSERT_EQ_INT(t, stats.total, 1);
    T_ASSERT_EQ_INT(t, stats.discharged, 1);
    T_ASSERT_EQ_INT(t, stats.emitted, 0);
    expect_check_stats(t, module, &stats);
    T_ASSERT(t, ir_verify(fix.dc, module));
    arena_free_all(&fix.arena);
}

void test_memsafe_instrument_orders_memcpy_source_before_destination(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "func void @f(ptr %dst, ptr %src) {\n"
                            "entry():\n"
                            "    memcpy %dst, %src, 8, align 1\n"
                            "    ret\n"
                            "}\n");
    MsCheckStats stats;
    const IrInst *read_check;
    const IrInst *write_check;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    ms_process_module(NULL, module, false, NULL, true, &stats);
    T_ASSERT_EQ_INT(t, stats.total, 2);
    T_ASSERT_EQ_INT(t, stats.discharged, 0);
    T_ASSERT_EQ_INT(t, stats.emitted, 2);
    expect_check_stats(t, module, &stats);
    read_check = module->funcs[0].blocks[0].first;
    write_check = read_check ? read_check->next : NULL;
    T_ASSERT(t, read_check && read_check->op == IR_CALL);
    T_ASSERT(t, write_check && write_check->op == IR_CALL);
    T_ASSERT(t, read_check && read_check->nops == 4);
    T_ASSERT(t, write_check && write_check->nops == 4);
    if (read_check && read_check->nops == 4)
        T_ASSERT_EQ_INT(t, read_check->ops[2].a, 0);
    if (write_check && write_check->nops == 4)
        T_ASSERT_EQ_INT(t, write_check->ops[2].a, 1);
    if (read_check && read_check->nops == 4)
        T_ASSERT_EQ_INT(t, read_check->ops[3].a, 1);
    if (write_check && write_check->nops == 4)
        T_ASSERT_EQ_INT(t, write_check->ops[3].a, 2);
    T_ASSERT(t, write_check && write_check->next &&
                    write_check->next->op == IR_MEMCPY);
    T_ASSERT(t, ir_verify(fix.dc, module));
    arena_free_all(&fix.arena);
}

void test_memsafe_instrument_covers_backend_va_start_writes(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module = summary_parse(&fix, "func void @f(ptr %ap, ...) {\n"
                                           "entry():\n"
                                           "    va_start %ap\n"
                                           "    ret\n"
                                           "}\n");
    MsCheckStats stats;
    const IrInst *check;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    ms_process_module(NULL, module, false, NULL, true, &stats);
    T_ASSERT_EQ_INT(t, stats.total, 1);
    T_ASSERT_EQ_INT(t, stats.discharged, 0);
    T_ASSERT_EQ_INT(t, stats.emitted, 1);
    expect_check_stats(t, module, &stats);
    check = module->funcs[0].blocks[0].first;
    T_ASSERT(t, check && check->op == IR_CALL && check->nops == 4);
    if (check && check->nops == 4) {
        T_ASSERT_EQ_INT(t, check->ops[1].a, 24);
        T_ASSERT_EQ_INT(t, check->ops[2].a, 1);
        T_ASSERT_EQ_INT(t, check->ops[3].a, 1);
    }
    T_ASSERT(t, check && check->next && check->next->op == IR_VA_START);
    T_ASSERT(t, ir_verify(fix.dc, module));
    arena_free_all(&fix.arena);
}

void test_memsafe_instrument_check_is_dce_side_effect_root(TestCtx *t)
{
    SummaryFix fix;
    IrModule *module =
        summary_parse(&fix, "func void @f(ptr %p) {\n"
                            "entry():\n"
                            "    %unused = load i32, %p, align 4, etype i32\n"
                            "    ret\n"
                            "}\n");
    MsCheckStats stats;
    OptConfig cfg;

    T_ASSERT(t, module != NULL);
    if (!module) {
        arena_free_all(&fix.arena);
        return;
    }
    ms_process_module(NULL, module, false, NULL, true, &stats);
    T_ASSERT_EQ_INT(t, stats.emitted, 1);
    expect_check_stats(t, module, &stats);
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, opt_dce(module, &cfg));
    T_ASSERT_EQ_INT(t, safe_check_count(module), 1);
    T_ASSERT(t, module->funcs[0].blocks[0].first &&
                    module->funcs[0].blocks[0].first->op == IR_CALL);
    T_ASSERT(t, module->funcs[0].blocks[0].first->next &&
                    module->funcs[0].blocks[0].first->next->op == IR_RET);
    T_ASSERT(t, ir_verify(fix.dc, module));
    arena_free_all(&fix.arena);
}
