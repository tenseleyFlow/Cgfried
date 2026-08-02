#ifndef CGF_TEST_DIRECTIVE_H
#define CGF_TEST_DIRECTIVE_H

#include <stdbool.h>
#include <stddef.h>

#include "util/arena.h"
#include "util/base.h"

/* Test-source directive language: `// NAME: value` comments, scanned from
 * the whole file. Parsing is pure (no I/O) so it unit-tests on strings.
 * Every rule here is anti-typo armor: an unknown ALL_CAPS directive, an
 * unknown target selector, or a malformed XF-id is a configuration error,
 * never a silently-ignored comment — a typo that silently matches nothing
 * converts a tracked bug into a green test. */

typedef enum {
    DIR_CHECK,
    DIR_EXIT_CODE,
    DIR_ERROR_EXPECTED,
    /* Like ERROR_EXPECTED but for a diagnostic that must NOT fail the
     * compile: the text must appear on stderr AND the exit code must stay
     * 0. Without this there is no way to pin warning behavior, and a
     * warning that silently stops firing is exactly the regression a
     * corpus is supposed to catch. */
    DIR_WARNING_EXPECTED,
    DIR_WARN_CHECK,
    DIR_WARN_COUNT,
    DIR_DIVERGES_GCC8, /* documented warning-oracle exception metadata */
    DIR_XFAIL,
    DIR_SKIP,
    DIR_TIMEOUT,
    DIR_FLAGS,  /* extra compiler argv (space-separated); -E switches the
                   pipeline to compiler-output checking */
    DIR_ENV,    /* NAME=VALUE for the compile step; repeatable */
    DIR_OPT_EQ, /* repeat the e2e pipeline at listed optimization levels */
    DIR_OFAST_DIVERGENCE_OK, /* closed license for -Ofast stdout divergence */
    DIR_ASM_CHECK,           /* reserved: hard-errors naming Sprint 24 */
    DIR_IR_CHECK,            /* CHECK semantics against `cgf -emit-ir` output
                                (.cgfir fixtures; live since Sprint 17) */
    DIR_MIR_CHECK,    /* CHECK semantics against `cgf -emit-mir` (Sprint 21) */
    DIR_IR_CHECK_NOT, /* negative form: the text must appear on NO output
                         line — how a golden proves an ABSENCE (Sprint
                         18's "short-circuit emits zero allocas") */
} DirectiveKind;

typedef struct {
    DirectiveKind kind;
    u32 line;              /* 1-based source line */
    const char *selector;  /* XFAIL/SKIP target selector, else NULL */
    const char *value;     /* text after ": " (XFAIL: reason after the id) */
    const char *xf_id;     /* XFAIL only: the cited XF-NNNN id */
    const char *warn_flag; /* WARN_CHECK only: flag name without "-W" */
} Directive;

typedef struct {
    u32 line;
    const char *msg;
} DirectiveError;

typedef struct {
    Directive *dirs;
    size_t ndirs;
    DirectiveError *errs; /* configuration errors; any => CONFIG ERROR */
    size_t nerrs;
    int exit_code;             /* EXIT_CODE value, default 0 */
    int timeout;               /* TIMEOUT value in seconds, 0 = none given */
    const char *flags;         /* FLAGS value (raw, space-separated), or NULL */
    const char *opt_levels[6]; /* canonical -O0/-O1/-O2/-O3/-Os/-Ofast */
    size_t nopt_levels;
    const char *ofast_divergence_reason;
    bool has_error_expected;
    bool has_warning_expected;
    bool has_warn_check;
    bool has_warn_count;
    int warn_count;
} DirectiveSet;

/* Parses directives out of a test source. All strings are arena-copies. */
void directive_parse(Arena *a, const char *src, size_t len, DirectiveSet *out);

/* `*` or an exact member of the closed target-name set. */
bool directive_selector_matches(const char *selector, const char *target);

#endif
