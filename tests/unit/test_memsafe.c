#include <string.h>

#include "memsafe/memsafe.h"
#include "unit.h"

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
        {"malloc", true, MS_NO_ARG, MS_NO_ARG, false, true, false, false},
        {"calloc", true, MS_NO_ARG, MS_NO_ARG, false, true, true, true},
        {"realloc", true, MS_NO_ARG, 0, true, true, false, false},
        {"reallocarray", true, MS_NO_ARG, 0, true, true, false, false},
        {"free", false, MS_NO_ARG, 0, false, false, false, false},
        {"strdup", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true},
        {"strndup", true, MS_NO_ARG, MS_NO_ARG, false, true, false, true},
        {"asprintf", true, 0, MS_NO_ARG, false, true, false, true},
        {"vasprintf", true, 0, MS_NO_ARG, false, true, false, true},
        {"aligned_alloc", true, MS_NO_ARG, MS_NO_ARG, false, true, false,
         false},
        {"posix_memalign", true, 0, MS_NO_ARG, false, true, false, false},
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
    const char *syms[] = {"malloc", "free", "posix_memalign"};
    AliasAllocSeed seed, *seeds = NULL;

    arena_init(&arena);
    module.syms = syms;
    module.nsyms = CGF_ARRAY_LEN(syms);
    malloc_call.op = IR_CALL;
    malloc_call.subop = FUNCREF_EXTERNAL;
    malloc_call.callee = 0;
    malloc_call.next = &free_call;
    free_call.op = IR_CALL;
    free_call.subop = FUNCREF_EXTERNAL;
    free_call.callee = 1;
    free_call.next = &posix_call;
    posix_call.op = IR_CALL;
    posix_call.subop = FUNCREF_EXTERNAL;
    posix_call.callee = 2;
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
    arena_free_all(&arena);
}
