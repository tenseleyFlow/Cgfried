#include <stdio.h>

#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

typedef struct WarnStateRow {
    WarnId id;
    bool default_on;
    bool wall_on;
    bool wextra_on;
} WarnStateRow;

static WarnCtx *state_ctx(Arena *arena)
{
    DiagCtx *dc = diag_ctx_new(arena);

    return warn_ctx_new(arena, dc);
}

static bool state_with(const char *flag, WarnId id)
{
    Arena arena;
    WarnCtx *warnings;
    bool enabled;

    arena_init(&arena);
    warnings = state_ctx(&arena);
    if (flag)
        (void)warn_flag(warnings, flag);
    enabled = warn_enabled(warnings, id, (Span){0});
    arena_free_all(&arena);
    return enabled;
}

void test_s38_warning_default_state_matrix(TestCtx *t)
{
    static const WarnStateRow rows[] = {
        {WARN_UNUSED_VARIABLE, false, true, false},
        {WARN_UNUSED_FUNCTION, false, true, false},
        {WARN_UNUSED_PARAMETER, false, false, false},
        {WARN_UNUSED_VALUE, false, true, false},
        {WARN_UNUSED_LABEL, false, true, false},
        {WARN_UNUSED_BUT_SET_VARIABLE, false, true, false},
        {WARN_UNUSED_BUT_SET_PARAMETER, false, false, false},
        {WARN_SHADOW, false, false, false},
        {WARN_SIGN_COMPARE, false, false, true},
        {WARN_CONVERSION, false, false, false},
        {WARN_SIGN_CONVERSION, false, false, false},
        {WARN_OVERFLOW, true, true, true},
        {WARN_IMPLICIT_FUNCTION_DECLARATION, true, true, true},
        {WARN_IMPLICIT_INT, true, true, true},
        {WARN_IMPLICIT_FALLTHROUGH, false, false, true},
        {WARN_PARENTHESES, false, true, false},
        {WARN_POINTER_ARITH, false, false, false},
        {WARN_CHAR_SUBSCRIPTS, false, true, false},
        {WARN_TYPE_LIMITS, false, false, true},
        {WARN_STRICT_PROTOTYPES, false, false, false},
        {WARN_OLD_STYLE_DEFINITION, false, false, false},
        {WARN_MISSING_PROTOTYPES, false, false, false},
        {WARN_MISSING_PARAMETER_TYPE, false, false, true},
        {WARN_OLD_STYLE_DECLARATION, false, false, true},
        {WARN_VLA, false, false, false},
        {WARN_SWITCH, false, true, false},
        {WARN_SWITCH_ENUM, false, false, false},
        {WARN_SWITCH_DEFAULT, false, false, false},
        {WARN_RETURN_TYPE, true, true, true},
        {WARN_ADDRESS, false, true, false},
        {WARN_BOOL_COMPARE, false, true, false},
        {WARN_TAUTOLOGICAL_COMPARE, false, true, false},
        {WARN_LOGICAL_NOT_PARENTHESES, false, true, false},
        {WARN_MISLEADING_INDENTATION, false, true, false},
    };
    size_t i;

    T_ASSERT(t, sizeof(rows) / sizeof(rows[0]) >= 34);
    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        const WarnInfo *info = warn_info_for_id(rows[i].id);
        char on[96];
        char off[96];

        T_ASSERT(t, info != NULL);
        if (!info)
            continue;
        (void)snprintf(on, sizeof(on), "-W%s", info->flag);
        (void)snprintf(off, sizeof(off), "-Wno-%s", info->flag);
        T_ASSERT_EQ_INT(t, state_with(NULL, rows[i].id), rows[i].default_on);
        T_ASSERT(t, state_with(on, rows[i].id));
        T_ASSERT(t, !state_with(off, rows[i].id));
        T_ASSERT_EQ_INT(t, state_with("-Wall", rows[i].id), rows[i].wall_on);
        T_ASSERT_EQ_INT(t, state_with("-Wextra", rows[i].id),
                        rows[i].wextra_on);
    }

    /* GCC C enables parameter-family unused warnings only at the
     * -Wunused/-Wextra intersection; -Wall supplies -Wunused. */
    {
        Arena arena;
        WarnCtx *warnings;

        arena_init(&arena);
        warnings = state_ctx(&arena);
        T_ASSERT(t, warn_flag(warnings, "-Wall"));
        T_ASSERT(t, warn_flag(warnings, "-Wextra"));
        T_ASSERT(t, warn_enabled(warnings, WARN_UNUSED_PARAMETER, (Span){0}));
        T_ASSERT(t, warn_enabled(warnings, WARN_UNUSED_BUT_SET_PARAMETER,
                                 (Span){0}));
        arena_free_all(&arena);
    }
}
