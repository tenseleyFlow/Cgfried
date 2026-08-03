#include <stdio.h>
#include <string.h>

#include "ir/ir.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

typedef struct WarnCapture {
    Diag last;
    int count;
} WarnCapture;

static void warn_capture(void *user, const Diag *d, const DiagCtx *dc)
{
    WarnCapture *cap = user;
    (void)dc;
    cap->last = *d;
    cap->count++;
}

static WarnCtx *new_warn(Arena *a, WarnCapture *cap)
{
    DiagCtx *dc = diag_ctx_new(a);
    DiagSink sink = {warn_capture, cap};

    memset(cap, 0, sizeof(*cap));
    diag_set_sink(dc, sink);
    return warn_ctx_new(a, dc);
}

void test_warn_flow_span_origin_is_part_of_dedup_identity(TestCtx *t)
{
    Arena a;
    WarnCapture cap;
    WarnCtx *w;
    IrModule *m;
    IrFunc *f;
    IrBuilder b;
    BlockId entry;
    Span function_span = {0};
    Span removed = {0};

    arena_init(&a);
    w = new_warn(&a, &cap);
    T_ASSERT(t, warn_flag(w, "-Wunreachable-code"));
    m = ir_module_new(&a, warn_diag(w));
    f = ir_func_new(m, "origin_dedup", IRT_VOID, NULL, 0);
    entry = ir_block_new(m, f, "entry");
    function_span.file_id =
        diag_add_file(warn_diag(w), "origin-dedup.c", "f\nx\n", 4);
    function_span.line = 1;
    function_span.col = 1;
    function_span.len = 1;
    f->loc = ir_intern_span(m, function_span);
    ir_builder_at(&b, m, f, entry);
    ir_builder_set_span(&b, function_span);
    ir_build_ret(&b, NULL);

    removed = function_span;
    removed.line = 2;
    removed.seq = 7;
    ir_func_record_removed_span(f, entry, removed, 0);
    removed.origin = SPAN_ORIGIN_ANY_MACRO;
    ir_func_record_removed_span(f, entry, removed, 0);

    warn_flow_module(w, m);
    T_ASSERT_EQ_INT(t, cap.count, 2);
    arena_free_all(&a);
}

static bool flag_in(const char *flag, const char *const *set, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
        if (strcmp(flag, set[i]) == 0)
            return true;
    return false;
}

void test_warn_metadata_groups(TestCtx *t)
{
    static const char *const wall[] = {"address",
                                       "array-bounds",
                                       "bool-compare",
                                       "bool-operation",
                                       "char-subscripts",
                                       "comment",
                                       "duplicate-decl-specifier",
                                       "enum-compare",
                                       "format",
                                       "format-contains-nul",
                                       "format-extra-args",
                                       "format-zero-length",
                                       "int-in-bool-context",
                                       "implicit",
                                       "implicit-function-declaration",
                                       "implicit-int",
                                       "logical-not-parentheses",
                                       "main",
                                       "maybe-uninitialized",
                                       "memset-elt-size",
                                       "memset-transposed-args",
                                       "misleading-indentation",
                                       "missing-braces",
                                       "multistatement-macros",
                                       "nonnull",
                                       "parentheses",
                                       "pointer-sign",
                                       "restrict",
                                       "return-type",
                                       "sequence-point",
                                       "sizeof-pointer-div",
                                       "sizeof-pointer-memaccess",
                                       "strict-aliasing",
                                       "strict-overflow",
                                       "stringop-truncation",
                                       "switch",
                                       "tautological-compare",
                                       "trigraphs",
                                       "uninitialized",
                                       "unknown-pragmas",
                                       "unused",
                                       "unused-but-set-variable",
                                       "unused-function",
                                       "unused-label",
                                       "unused-local-typedefs",
                                       "unused-value",
                                       "unused-variable",
                                       "volatile-register-var"};
    static const char *const extra[] = {
        "cast-function-type",
        "clobbered",
        "empty-body",
        "ignored-qualifiers",
        "implicit-fallthrough",
        "missing-field-initializers",
        "missing-parameter-type",
        "old-style-declaration",
        "override-init",
        "pointer-compared-to-zero-with-relational",
        "shift-negative-value",
        "sign-compare",
        "type-limits",
        "uninitialized",
        "unused-but-set-parameter",
        "unused-parameter"};
    size_t i, nwall = 0, nextra = 0;
    const char *prev = "";

    T_ASSERT_EQ_INT(t, sizeof(wall) / sizeof(wall[0]), 48);
    T_ASSERT_EQ_INT(t, sizeof(extra) / sizeof(extra[0]), 16);
    for (i = 0; i < warn_info_count(); i++) {
        const WarnInfo *info = warn_info_at(i);
        T_ASSERT(t, strcmp(prev, info->flag) < 0);
        prev = info->flag;
        if (info->groups & WG_ALL) {
            nwall++;
            T_ASSERT(t,
                     flag_in(info->flag, wall, sizeof(wall) / sizeof(wall[0])));
        }
        if (info->groups & WG_EXTRA) {
            nextra++;
            T_ASSERT(t, flag_in(info->flag, extra,
                                sizeof(extra) / sizeof(extra[0])));
        }
    }
    T_ASSERT_EQ_INT(t, nwall, sizeof(wall) / sizeof(wall[0]));
    T_ASSERT_EQ_INT(t, nextra, sizeof(extra) / sizeof(extra[0]));
    T_ASSERT_EQ_INT(t, warn_info_for_flag("-Warray-bounds=1")->id,
                    WARN_ARRAY_BOUNDS);
    T_ASSERT_EQ_INT(t, warn_info_for_flag("implicit-fallthrough=3")->id,
                    WARN_IMPLICIT_FALLTHROUGH);
}

typedef struct FlagCase {
    const char *opts[5];
    WarnId id;
    int count;
    DiagLevel level;
} FlagCase;

void test_warn_flag_order_table(TestCtx *t)
{
    static const FlagCase cases[] = {
        {{NULL}, WARN_UNUSED_VARIABLE, 0, DIAG_WARNING},
        {{"-Wall"}, WARN_UNUSED_VARIABLE, 1, DIAG_WARNING},
        {{"-Wno-unused-variable", "-Wall"},
         WARN_UNUSED_VARIABLE,
         0,
         DIAG_WARNING},
        {{"-Wall", "-Wno-unused-variable"},
         WARN_UNUSED_VARIABLE,
         0,
         DIAG_WARNING},
        {{"-Wunused-variable", "-Wno-all"},
         WARN_UNUSED_VARIABLE,
         1,
         DIAG_WARNING},
        {{"-Wno-all", "-Wunused-variable"},
         WARN_UNUSED_VARIABLE,
         1,
         DIAG_WARNING},
        {{"-Wunused", "-Wno-unused-variable"},
         WARN_UNUSED_VARIABLE,
         0,
         DIAG_WARNING},
        {{"-Wno-unused-variable", "-Wunused"},
         WARN_UNUSED_VARIABLE,
         0,
         DIAG_WARNING},
        {{"-Wall", "-Werror"}, WARN_UNUSED_VARIABLE, 1, DIAG_ERROR},
        {{"-Werror", "-Wall"}, WARN_UNUSED_VARIABLE, 1, DIAG_ERROR},
        {{"-Werror=unused-variable"}, WARN_UNUSED_VARIABLE, 1, DIAG_ERROR},
        {{"-Werror=unused-variable", "-Wno-unused-variable"},
         WARN_UNUSED_VARIABLE,
         0,
         DIAG_WARNING},
        {{"-Wno-unused-variable", "-Werror=unused-variable"},
         WARN_UNUSED_VARIABLE,
         1,
         DIAG_ERROR},
        {{"-Wno-error=unused-variable", "-Werror", "-Wall"},
         WARN_UNUSED_VARIABLE,
         1,
         DIAG_WARNING},
        {{"-Werror", "-Wno-error=unused-variable", "-Wall"},
         WARN_UNUSED_VARIABLE,
         1,
         DIAG_WARNING},
        {{"-Wall", "-Werror", "-Wno-error"},
         WARN_UNUSED_VARIABLE,
         1,
         DIAG_WARNING},
        {{"-Wall", "-Wno-error", "-Werror"},
         WARN_UNUSED_VARIABLE,
         1,
         DIAG_ERROR},
        {{"-w", "-Wall"}, WARN_UNUSED_VARIABLE, 0, DIAG_WARNING},
        {{"-Wall", "-w"}, WARN_UNUSED_VARIABLE, 0, DIAG_WARNING},
        {{"-w", "-Werror=unused-variable"},
         WARN_UNUSED_VARIABLE,
         0,
         DIAG_ERROR},
        {{"-Wpragmas"}, WARN_PRAGMAS, 1, DIAG_WARNING},
        {{"-Wno-pragmas"}, WARN_PRAGMAS, 0, DIAG_WARNING},
        {{"-Wno-pragmas", "-Wpragmas"}, WARN_PRAGMAS, 1, DIAG_WARNING},
        {{"-Wpragmas", "-Wno-pragmas"}, WARN_PRAGMAS, 0, DIAG_WARNING},
        {{"-Wextra"}, WARN_SIGN_COMPARE, 1, DIAG_WARNING},
        {{"-Wno-extra", "-Wsign-compare"}, WARN_SIGN_COMPARE, 1, DIAG_WARNING},
        {{"-Wsign-compare", "-Wno-extra"}, WARN_SIGN_COMPARE, 1, DIAG_WARNING},
        {{"-Wextra", "-Wno-sign-compare"}, WARN_SIGN_COMPARE, 0, DIAG_WARNING},
        {{"-Wextra"}, WARN_UNUSED_PARAMETER, 0, DIAG_WARNING},
        {{"-Wunused"}, WARN_UNUSED_PARAMETER, 0, DIAG_WARNING},
        {{"-Wextra", "-Wunused"}, WARN_UNUSED_PARAMETER, 1, DIAG_WARNING},
        {{"-Wunused", "-Wextra"}, WARN_UNUSED_PARAMETER, 1, DIAG_WARNING},
        {{"-Wall", "-Wextra"}, WARN_UNUSED_PARAMETER, 1, DIAG_WARNING},
        {{"-Wextra", "-Wall"}, WARN_UNUSED_PARAMETER, 1, DIAG_WARNING},
        {{"-Wno-unused", "-Wall", "-Wextra"},
         WARN_UNUSED_PARAMETER,
         0,
         DIAG_WARNING},
        {{"-Wextra", "-Wunused", "-Wno-extra"},
         WARN_UNUSED_PARAMETER,
         0,
         DIAG_WARNING},
        {{"-Wformat"}, WARN_FORMAT, 1, DIAG_WARNING},
        {{"-Wformat"}, WARN_FORMAT_EXTRA_ARGS, 1, DIAG_WARNING},
        {{"-Wformat"}, WARN_NONNULL, 1, DIAG_WARNING},
        {{"-Wno-format", "-Wall"}, WARN_NONNULL, 1, DIAG_WARNING},
        {{"-Wall", "-Wno-format"}, WARN_NONNULL, 0, DIAG_WARNING},
        {{"-Wformat", "-Wno-format"}, WARN_NONNULL, 0, DIAG_WARNING},
        {{"-Wformat"}, WARN_FORMAT_SECURITY, 0, DIAG_WARNING},
        {{"-Wformat=0"}, WARN_FORMAT, 0, DIAG_WARNING},
        {{"-Wformat=1"}, WARN_FORMAT_SECURITY, 0, DIAG_WARNING},
        {{"-Wformat=2"}, WARN_FORMAT_SECURITY, 1, DIAG_WARNING},
        {{"-Wformat=2"}, WARN_FORMAT_SIGNEDNESS, 0, DIAG_WARNING},
        {{"-Wformat=2", "-Wno-format-extra-args"},
         WARN_FORMAT_EXTRA_ARGS,
         0,
         DIAG_WARNING},
        {{"-Werror=format=2"}, WARN_FORMAT_SECURITY, 1, DIAG_ERROR},
        {{"-Werror=format=2"}, WARN_FORMAT_EXTRA_ARGS, 1, DIAG_ERROR},
        {{"-Wformat=2", "-Wformat=1"}, WARN_FORMAT_SECURITY, 0, DIAG_WARNING},
        {{"-Wno-format-security", "-Wformat=2"},
         WARN_FORMAT_SECURITY,
         0,
         DIAG_WARNING},
        {{"-Wformat=2", "-Wno-format-security"},
         WARN_FORMAT_SECURITY,
         0,
         DIAG_WARNING},
        {{"-Warray-bounds=1"}, WARN_ARRAY_BOUNDS, 1, DIAG_WARNING},
        {{"-Wno-array-bounds=2"}, WARN_ARRAY_BOUNDS, 0, DIAG_WARNING},
        {{"-Wimplicit"}, WARN_IMPLICIT_INT, 1, DIAG_WARNING},
        {{"-Wimplicit", "-Wno-implicit-int"},
         WARN_IMPLICIT_INT,
         0,
         DIAG_WARNING},
        {{"-Wno-implicit-int", "-Wimplicit"},
         WARN_IMPLICIT_INT,
         0,
         DIAG_WARNING},
        {{"-Wpedantic"}, WARN_POINTER_ARITH, 1, DIAG_WARNING},
        {{"-Wno-pedantic", "-Wpointer-arith"},
         WARN_POINTER_ARITH,
         1,
         DIAG_WARNING},
        {{"-pedantic-errors"}, WARN_POINTER_ARITH, 1, DIAG_ERROR},
        {{"-pedantic-errors", "-w"}, WARN_POINTER_ARITH, 0, DIAG_ERROR},
        {{"-Werror", "-Wpointer-arith"}, WARN_POINTER_ARITH, 1, DIAG_ERROR},
        {{"-Wpointer-arith", "-Wno-error=pointer-arith", "-Werror"},
         WARN_POINTER_ARITH,
         1,
         DIAG_WARNING},
        {{"-Werror=unused"}, WARN_UNUSED_VARIABLE, 1, DIAG_ERROR},
        {{"-Wall", "-Werror=unused", "-Wno-error=unused-variable"},
         WARN_UNUSED_VARIABLE,
         1,
         DIAG_WARNING},
    };
    size_t i, j;

    T_ASSERT(t, sizeof(cases) / sizeof(cases[0]) >= 40);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Arena a;
        WarnCapture cap;
        WarnCtx *w;
        arena_init(&a);
        w = new_warn(&a, &cap);
        for (j = 0; j < sizeof(cases[i].opts) / sizeof(cases[i].opts[0]) &&
                    cases[i].opts[j];
             j++)
            T_ASSERT(t, warn_flag(w, cases[i].opts[j]));
        warn_at(w, cases[i].id, (Span){0}, "probe");
        T_ASSERT_EQ_INT(t, cap.count, cases[i].count);
        if (cap.count) {
            T_ASSERT_EQ_INT(t, cap.last.level, cases[i].level);
            T_ASSERT_EQ_INT(t, cap.last.warn_id, cases[i].id);
        }
        arena_free_all(&a);
    }
}

void test_warn_implicit_fallthrough_levels(TestCtx *t)
{
    static const struct {
        const char *opts[4];
        unsigned level;
        bool enabled;
    } cases[] = {
        {{NULL}, 0, false},
        {{"-Wextra"}, 3, true},
        {{"-Wextra", "-Wno-extra"}, 0, false},
        {{"-Wimplicit-fallthrough"}, 3, true},
        {{"-Wno-implicit-fallthrough"}, 0, false},
        {{"-Wimplicit-fallthrough=0"}, 0, false},
        {{"-Wimplicit-fallthrough=1"}, 1, true},
        {{"-Wimplicit-fallthrough=2"}, 2, true},
        {{"-Wimplicit-fallthrough=3"}, 3, true},
        {{"-Wimplicit-fallthrough=4"}, 4, true},
        {{"-Wimplicit-fallthrough=5"}, 5, true},
        {{"-Wimplicit-fallthrough=4", "-Wno-extra"}, 4, true},
        {{"-Wno-extra", "-Wimplicit-fallthrough=2"}, 2, true},
        {{"-Wno-implicit-fallthrough", "-Wextra"}, 0, false},
        {{"-Werror=implicit-fallthrough=5"}, 5, true},
        {{"-Werror=implicit-fallthrough=0"}, 0, false},
    };
    size_t i, j;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Arena a;
        WarnCapture cap;
        WarnCtx *w;

        arena_init(&a);
        w = new_warn(&a, &cap);
        for (j = 0; j < sizeof(cases[i].opts) / sizeof(cases[i].opts[0]) &&
                    cases[i].opts[j];
             j++)
            T_ASSERT(t, warn_flag(w, cases[i].opts[j]));
        T_ASSERT_EQ_INT(t, warn_implicit_fallthrough_level(w), cases[i].level);
        T_ASSERT_EQ_INT(t,
                        warn_enabled(w, WARN_IMPLICIT_FALLTHROUGH, (Span){0}),
                        cases[i].enabled);
        arena_free_all(&a);
    }

    T_ASSERT_EQ_INT(t, warn_implicit_fallthrough_level(NULL), 0);
}

void test_warn_explicit_activation(TestCtx *t)
{
    Arena a;
    WarnCapture cap;
    WarnCtx *w;
    Span sp = {0};

    arena_init(&a);
    w = new_warn(&a, &cap);
    T_ASSERT(t, warn_enabled(w, WARN_IMPLICIT_INT, sp));
    T_ASSERT(t, !warn_explicitly_enabled(w, WARN_IMPLICIT_INT, sp));
    T_ASSERT(t, warn_flag(w, "-Wall"));
    T_ASSERT(t, warn_explicitly_enabled(w, WARN_IMPLICIT_INT, sp));
    T_ASSERT(t, warn_flag(w, "-Wno-implicit-int"));
    T_ASSERT(t, !warn_explicitly_enabled(w, WARN_IMPLICIT_INT, sp));
    T_ASSERT(t, warn_flag(w, "-Wimplicit-int"));
    T_ASSERT(t, warn_explicitly_enabled(w, WARN_IMPLICIT_INT, sp));
    arena_free_all(&a);

    arena_init(&a);
    w = new_warn(&a, &cap);
    warn_pragma_set(w, 10, WARN_IMPLICIT_INT, WARN_PRAGMA_WARNING);
    warn_pragma_set(w, 20, WARN_IMPLICIT_INT, WARN_PRAGMA_IGNORED);
    warn_pragma_set(w, 30, WARN_IMPLICIT_INT, WARN_PRAGMA_ERROR);
    sp.seq = 5;
    T_ASSERT(t, !warn_explicitly_enabled(w, WARN_IMPLICIT_INT, sp));
    sp.seq = 15;
    T_ASSERT(t, warn_explicitly_enabled(w, WARN_IMPLICIT_INT, sp));
    sp.seq = 25;
    T_ASSERT(t, !warn_explicitly_enabled(w, WARN_IMPLICIT_INT, sp));
    sp.seq = 35;
    T_ASSERT(t, warn_explicitly_enabled(w, WARN_IMPLICIT_INT, sp));
    sp.origin = SPAN_ORIGIN_SYSTEM_SPELLING;
    T_ASSERT(t, !warn_explicitly_enabled(w, WARN_IMPLICIT_INT, sp));
    arena_free_all(&a);

    T_ASSERT(t, !warn_explicitly_enabled(NULL, WARN_IMPLICIT_INT, (Span){0}));
}

void test_warn_memsafe_policy(TestCtx *t)
{
    static const WarnId proof_ids[] = {
        WARN_MEM_ANNOTATION_MISMATCH, WARN_MEM_DOUBLE_FREE,
        WARN_MEM_FREE_NONHEAP,        WARN_MEM_LEAK,
        WARN_MEM_OUT_OF_BOUNDS,       WARN_MEM_UNINIT_READ,
        WARN_MEM_USE_AFTER_FREE,
    };
    Arena a;
    WarnCapture cap;
    WarnCtx *w;
    size_t i;

    arena_init(&a);
    w = new_warn(&a, &cap);
    for (i = 0; i < CGF_ARRAY_LEN(proof_ids); i++)
        T_ASSERT(t, warn_enabled(w, proof_ids[i], (Span){0}));
    T_ASSERT(t, !warn_enabled(w, WARN_MEM_REALLOC_ZERO, (Span){0}));
    T_ASSERT(t, !warn_enabled(w, WARN_MEM_NULL_CHECK, (Span){0}));
    T_ASSERT(t, !warn_enabled(w, WARN_MEM_SIZEOF_MISMATCH, (Span){0}));
    T_ASSERT(t, !warn_enabled(w, WARN_MEM_SUGGEST_ANNOTATIONS, (Span){0}));
    T_ASSERT(t, !warn_enabled(w, WARN_MEM_UNBOUNDED_COPY, (Span){0}));
    T_ASSERT(t, warn_memsafe_needed(w));
    T_ASSERT(t, !warn_mem_strict_enabled(w));
    T_ASSERT(t, !warn_mem_strict_enabled(NULL));
    T_ASSERT_EQ_INT(t, warn_pragma_option_id("-Wmem-leak"), WARN_MEM_LEAK);
    T_ASSERT_EQ_INT(t, warn_pragma_option_id("-Wmem-annotation-mismatch"),
                    WARN_MEM_ANNOTATION_MISMATCH);
    T_ASSERT_EQ_INT(t, warn_option_classify("-Wmem"), WARN_OPTION_KNOWN);
    T_ASSERT_EQ_INT(t, warn_option_classify("-Wmem-strict"), WARN_OPTION_KNOWN);

    T_ASSERT(t, warn_flag(w, "-Wno-mem"));
    for (i = 0; i < CGF_ARRAY_LEN(proof_ids); i++)
        T_ASSERT(t, !warn_enabled(w, proof_ids[i], (Span){0}));
    T_ASSERT(t, !warn_memsafe_needed(w));

    T_ASSERT(t, warn_flag(w, "-Wmem-use-after-free"));
    T_ASSERT(t, warn_enabled(w, WARN_MEM_USE_AFTER_FREE, (Span){0}));
    T_ASSERT(t, warn_memsafe_needed(w));
    T_ASSERT(t, warn_flag(w, "-Wmem-strict"));
    T_ASSERT(t, warn_mem_strict_enabled(w));
    T_ASSERT(t, warn_enabled(w, WARN_MEM_NULL_CHECK, (Span){0}));
    T_ASSERT(t, warn_enabled(w, WARN_MEM_SIZEOF_MISMATCH, (Span){0}));
    T_ASSERT(t, warn_enabled(w, WARN_MEM_UNBOUNDED_COPY, (Span){0}));
    T_ASSERT(t, warn_flag(w, "-Wno-mem-strict"));
    T_ASSERT(t, !warn_mem_strict_enabled(w));
    T_ASSERT(t, warn_enabled(w, WARN_MEM_USE_AFTER_FREE, (Span){0}));
    T_ASSERT(t, warn_flag(w, "-Wmem-realloc-zero"));
    T_ASSERT(t, warn_enabled(w, WARN_MEM_REALLOC_ZERO, (Span){0}));
    arena_free_all(&a);

    arena_init(&a);
    w = new_warn(&a, &cap);
    T_ASSERT(t, warn_flag(w, "-Wno-mem"));
    T_ASSERT(t, warn_flag(w, "-Werror=mem"));
    {
        DiagFixit fix = {{0}, "repair", true};

        warn_at_fixits(w, WARN_MEM_LEAK, (Span){0}, &fix, 1, "probe %d", 7);
    }
    T_ASSERT_EQ_INT(t, cap.count, 1);
    T_ASSERT_EQ_INT(t, cap.last.level, DIAG_ERROR);
    T_ASSERT_EQ_INT(t, cap.last.warn_id, WARN_MEM_LEAK);
    T_ASSERT_EQ_STR(t, cap.last.message, "probe 7");
    T_ASSERT_EQ_INT(t, cap.last.fixit_count, 1);
    T_ASSERT_EQ_STR(t, cap.last.fixits[0].insert, "repair");
    arena_free_all(&a);

    arena_init(&a);
    w = new_warn(&a, &cap);
    T_ASSERT(t, warn_flag(w, "-Wno-mem"));
    warn_pragma_set(w, 10, WARN_MEM_LEAK, WARN_PRAGMA_WARNING);
    T_ASSERT(t, warn_memsafe_needed(w));
    T_ASSERT(t, !warn_enabled(w, WARN_MEM_LEAK, (Span){.seq = 5}));
    T_ASSERT(t, warn_enabled(w, WARN_MEM_LEAK, (Span){.seq = 10}));
    arena_free_all(&a);
}

void test_warn_pedwarn_exhaustive(TestCtx *t)
{
    static const char *const configs[] = {NULL, "-pedantic",
                                          "-pedantic-errors"};
    static const int counts[3][3] = {{0, 1, 1}, {1, 1, 1}, {1, 1, 1}};
    static const DiagLevel levels[3][3] = {
        {DIAG_WARNING, DIAG_WARNING, DIAG_WARNING},
        {DIAG_WARNING, DIAG_WARNING, DIAG_WARNING},
        {DIAG_ERROR, DIAG_ERROR, DIAG_WARNING}};
    size_t row, col;

    for (row = 0; row < 3; row++) {
        for (col = 0; col < 3; col++) {
            Arena a;
            WarnCapture cap;
            WarnCtx *w;
            WarnId id = col == 2 ? WARN_PRAGMAS : WARN_POINTER_ARITH;
            arena_init(&a);
            w = new_warn(&a, &cap);
            if (configs[row])
                T_ASSERT(t, warn_flag(w, configs[row]));
            if (col == 1)
                T_ASSERT(t, warn_flag(w, "-Wpointer-arith"));
            if (col == 2)
                warn_at(w, id, (Span){0}, "warn probe");
            else
                warn_at(w, id, (Span){0}, "ped probe");
            T_ASSERT_EQ_INT(t, cap.count, counts[row][col]);
            if (cap.count)
                T_ASSERT_EQ_INT(t, cap.last.level, levels[row][col]);
            arena_free_all(&a);
        }
    }

    /* One registry flag can classify both ordinary warnings and pedwarn
     * occurrences. Only the latter are promoted by -pedantic-errors. */
    for (row = 0; row < 3; row++) {
        Arena a;
        WarnCapture cap;
        WarnCtx *w;

        arena_init(&a);
        w = new_warn(&a, &cap);
        if (configs[row])
            T_ASSERT(t, warn_flag(w, configs[row]));
        warn_pedwarn_at(w, WARN_OVERFLOW, (Span){0}, "integer overflow");
        T_ASSERT_EQ_INT(t, cap.count, 1);
        T_ASSERT_EQ_INT(t, cap.last.level,
                        row == 2 ? DIAG_ERROR : DIAG_WARNING);
        warn_at(w, WARN_OVERFLOW, (Span){0}, "floating overflow");
        T_ASSERT_EQ_INT(t, cap.count, 2);
        T_ASSERT_EQ_INT(t, cap.last.level, DIAG_WARNING);
        arena_free_all(&a);
    }
}

void test_warn_pragma_sequence_and_stack(TestCtx *t)
{
    Arena a;
    WarnCapture cap;
    WarnCtx *w;
    Span sp = {0};

    arena_init(&a);
    w = new_warn(&a, &cap);
    warn_pragma_set(w, 10, WARN_PRAGMAS, WARN_PRAGMA_IGNORED);
    warn_pragma_push(w, 20);
    warn_pragma_set(w, 30, WARN_PRAGMAS, WARN_PRAGMA_ERROR);
    warn_pragma_push(w, 40);
    warn_pragma_set(w, 50, WARN_PRAGMAS, WARN_PRAGMA_IGNORED);
    T_ASSERT(t, warn_pragma_pop(w, 60, sp));
    T_ASSERT(t, warn_pragma_pop(w, 70, sp));

    sp.seq = 5;
    warn_at(w, WARN_PRAGMAS, sp, "a");
    T_ASSERT_EQ_INT(t, cap.count, 1);
    T_ASSERT_EQ_INT(t, cap.last.level, DIAG_WARNING);
    sp.seq = 15;
    warn_at(w, WARN_PRAGMAS, sp, "b");
    T_ASSERT_EQ_INT(t, cap.count, 1);
    sp.seq = 35;
    warn_at(w, WARN_PRAGMAS, sp, "c");
    T_ASSERT_EQ_INT(t, cap.count, 2);
    T_ASSERT_EQ_INT(t, cap.last.level, DIAG_ERROR);
    sp.seq = 55;
    warn_at(w, WARN_PRAGMAS, sp, "d");
    T_ASSERT_EQ_INT(t, cap.count, 2);
    sp.seq = 65;
    warn_at(w, WARN_PRAGMAS, sp, "e");
    T_ASSERT_EQ_INT(t, cap.count, 3);
    T_ASSERT_EQ_INT(t, cap.last.level, DIAG_ERROR);
    sp.seq = 75;
    warn_at(w, WARN_PRAGMAS, sp, "f");
    T_ASSERT_EQ_INT(t, cap.count, 3);
    T_ASSERT(t, !warn_pragma_pop(w, 80, sp));
    T_ASSERT_EQ_INT(t, cap.count, 3);
    sp.seq = 85;
    warn_at(w, WARN_PRAGMAS, sp, "baseline restored");
    T_ASSERT_EQ_INT(t, cap.count, 4);
    arena_free_all(&a);

    arena_init(&a);
    w = new_warn(&a, &cap);
    T_ASSERT(t, !warn_pragma_pop(w, 1, (Span){0}));
    T_ASSERT_EQ_INT(t, cap.count, 0);
    arena_free_all(&a);
}

void test_warn_suffix_id_and_suppression(TestCtx *t)
{
    Arena a;
    WarnCapture cap;
    WarnCtx *w;
    Span sp = {0};
    FILE *f;
    char rendered[128];
    size_t n;

    arena_init(&a);
    w = new_warn(&a, &cap);
    sp.origin = SPAN_ORIGIN_SYSTEM_SPELLING;
    warn_at(w, WARN_PRAGMAS, sp, "hidden");
    T_ASSERT_EQ_INT(t, cap.count, 0);
    T_ASSERT(t, warn_flag(w, "-Werror=pragmas"));
    warn_at(w, WARN_PRAGMAS, sp, "promoted but hidden");
    T_ASSERT_EQ_INT(t, cap.count, 0);
    sp.origin = SPAN_ORIGIN_SYSTEM_MACRO | SPAN_ORIGIN_ANY_MACRO;
    warn_at(w, WARN_PRAGMAS, sp, "system macro hidden");
    T_ASSERT_EQ_INT(t, cap.count, 0);
    T_ASSERT(t, warn_flag(w, "-Wsystem-headers"));
    warn_at(w, WARN_PRAGMAS, sp, "visible");
    T_ASSERT_EQ_INT(t, cap.count, 1);
    T_ASSERT_EQ_INT(t, cap.last.warn_id, WARN_PRAGMAS);
    sp.origin = SPAN_ORIGIN_SYSTEM_MACRO | SPAN_ORIGIN_ANY_MACRO;
    warn_at_ex(w, WARN_PRAGMAS, sp, WARN_SUPPRESS_IN_MACRO,
               "macro blanket hidden");
    T_ASSERT_EQ_INT(t, cap.count, 1);
    T_ASSERT_EQ_INT(t, cap.last.level, DIAG_ERROR);

    f = tmpfile();
    T_ASSERT(t, f != NULL);
    diag_render(f, &cap.last, warn_diag(w), false);
    rewind(f);
    n = fread(rendered, 1, sizeof(rendered) - 1, f);
    rendered[n] = '\0';
    fclose(f);
    T_ASSERT_EQ_STR(t, rendered, "cgfried: error: visible [-Werror=pragmas]\n");

    T_ASSERT(t, warn_option_known("-Wall"));
    T_ASSERT(t, warn_option_known("-Wformat=2"));
    T_ASSERT(t, warn_option_known("-Werror=format=2"));
    T_ASSERT(t, !warn_option_known("-Wformat=3"));
    T_ASSERT(t, !warn_option_known("-Wno-format=2"));
    T_ASSERT(t, !warn_option_known("-Wformat-security=2"));
    T_ASSERT(t, !warn_option_known("-Werror=format-security=2"));
    T_ASSERT(t, warn_option_known("-Wmaybe-uninitialized=strict"));
    T_ASSERT(t, warn_option_known("-Wno-maybe-uninitialized"));
    T_ASSERT(t, !warn_option_known("-Wmaybe-uninitialized=lax"));
    T_ASSERT(t, !warn_flag(w, "-Wformat=3"));
    T_ASSERT(t, !warn_flag(w, "-Wno-format=2"));
    T_ASSERT(t, !warn_flag(w, "-Wformat-security=2"));
    T_ASSERT(t, warn_option_known("-Werror=unused-variable"));
    T_ASSERT(t, warn_option_known("-Wno-error=unused-variable"));
    T_ASSERT(t, !warn_option_known("-Wnot-a-real-warning"));
    T_ASSERT(t, !warn_option_known("-Werror=not-a-real-warning"));
    T_ASSERT_EQ_INT(t, warn_option_classify("-Wformat=3"),
                    WARN_OPTION_BAD_FORMAT_LEVEL);
    T_ASSERT_EQ_INT(t, warn_option_classify("-Wformat-security=2"),
                    WARN_OPTION_UNKNOWN_POSITIVE);
    T_ASSERT_EQ_INT(t, warn_option_classify("-Werror=format-security=2"),
                    WARN_OPTION_UNKNOWN_PROMOTION);
    T_ASSERT_EQ_STR(t, warn_flag_name(WARN_FORMAT), "format=");
    T_ASSERT_EQ_STR(t, warn_flag_name(WARN_FORMAT_SIGNEDNESS), "format=");
    T_ASSERT_EQ_STR(t,
                    warn_option_bad_value_label(WARN_OPTION_BAD_FORMAT_LEVEL),
                    "-Wformat=");
    T_ASSERT(t, warn_option_known("-Wimplicit-fallthrough=0"));
    T_ASSERT(t, warn_option_known("-Wimplicit-fallthrough=5"));
    T_ASSERT(t, warn_option_known("-Werror=implicit-fallthrough=4"));
    T_ASSERT(t, warn_option_known("-Wno-error=implicit-fallthrough=4"));
    T_ASSERT(t, !warn_option_known("-Wimplicit-fallthrough=6"));
    T_ASSERT(t, !warn_option_known("-Wno-implicit-fallthrough=4"));
    T_ASSERT_EQ_INT(t, warn_option_classify("-Wimplicit-fallthrough=6"),
                    WARN_OPTION_BAD_IMPLICIT_FALLTHROUGH_LEVEL);
    T_ASSERT_EQ_INT(t, warn_option_classify("-Wno-implicit-fallthrough=4"),
                    WARN_OPTION_BAD_IMPLICIT_FALLTHROUGH_LEVEL);
    T_ASSERT_EQ_STR(
        t,
        warn_option_bad_value_label(WARN_OPTION_BAD_IMPLICIT_FALLTHROUGH_LEVEL),
        "-Wimplicit-fallthrough=");
    T_ASSERT_EQ_INT(t, warn_option_classify("-Wmaybe-uninitialized=lax"),
                    WARN_OPTION_BAD_MAYBE_UNINITIALIZED_LEVEL);
    T_ASSERT_EQ_STR(
        t,
        warn_option_bad_value_label(WARN_OPTION_BAD_MAYBE_UNINITIALIZED_LEVEL),
        "-Wmaybe-uninitialized=");
    T_ASSERT_EQ_INT(t, warn_option_classify("-Wnot-a-real-warning"),
                    WARN_OPTION_UNKNOWN_POSITIVE);
    T_ASSERT_EQ_INT(t, warn_option_classify("-Wno-not-a-real-warning"),
                    WARN_OPTION_UNKNOWN_NEGATIVE);
    T_ASSERT_EQ_INT(t, warn_option_classify("-Werror=not-a-real-warning"),
                    WARN_OPTION_UNKNOWN_PROMOTION);
    T_ASSERT_EQ_INT(t, warn_pragma_option_id("-Wpragmas"), WARN_PRAGMAS);
    T_ASSERT_EQ_INT(t, warn_pragma_option_id("-Wno-pragmas"), WARN_NONE);
    T_ASSERT_EQ_INT(t, warn_pragma_option_id("-Wformat=2"), WARN_NONE);
    arena_free_all(&a);

    arena_init(&a);
    w = new_warn(&a, &cap);
    T_ASSERT(t, !warn_maybe_uninitialized_strict(w));
    T_ASSERT(t, warn_flag(w, "-Wmaybe-uninitialized=strict"));
    T_ASSERT(t, warn_maybe_uninitialized_strict(w));
    T_ASSERT(t, warn_enabled(w, WARN_MAYBE_UNINITIALIZED, (Span){0}));
    T_ASSERT(t, warn_flag(w, "-Wno-maybe-uninitialized"));
    T_ASSERT(t, !warn_maybe_uninitialized_strict(w));
    T_ASSERT(t, !warn_enabled(w, WARN_MAYBE_UNINITIALIZED, (Span){0}));
    arena_free_all(&a);

    arena_init(&a);
    w = new_warn(&a, &cap);
    T_ASSERT(t, warn_flag(w, "-Wall"));
    warn_at(w, WARN_UNKNOWN_PRAGMAS, (Span){0}, "unknown tracer");
    T_ASSERT_EQ_INT(t, cap.count, 1);
    T_ASSERT_EQ_INT(t, cap.last.warn_id, WARN_UNKNOWN_PRAGMAS);
    arena_free_all(&a);
}
