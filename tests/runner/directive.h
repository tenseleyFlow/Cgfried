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
    DIR_XFAIL,
    DIR_SKIP,
    DIR_TIMEOUT,
    DIR_OPT_EQ,    /* reserved: hard-errors naming Sprint 30 */
    DIR_ASM_CHECK, /* reserved: hard-errors naming Sprint 24 */
    DIR_IR_CHECK,  /* reserved: hard-errors naming Sprint 17 */
} DirectiveKind;

typedef struct {
    DirectiveKind kind;
    u32 line;             /* 1-based source line */
    const char *selector; /* XFAIL/SKIP target selector, else NULL */
    const char *value;    /* text after ": " (XFAIL: reason after the id) */
    const char *xf_id;    /* XFAIL only: the cited XF-NNNN id */
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
    int exit_code; /* EXIT_CODE value, default 0 */
    int timeout;   /* TIMEOUT value in seconds, 0 = none given */
    bool has_error_expected;
} DirectiveSet;

/* Parses directives out of a test source. All strings are arena-copies. */
void directive_parse(Arena *a, const char *src, size_t len, DirectiveSet *out);

/* `*` or an exact member of the closed target-name set. */
bool directive_selector_matches(const char *selector, const char *target);

#endif
