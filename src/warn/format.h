#ifndef CGF_WARN_FORMAT_H
#define CGF_WARN_FORMAT_H

#include <stdbool.h>

#include "target.h"
#include "util/base.h"

typedef enum { FMT_PRINTF, FMT_SCANF, FMT_STRFTIME, FMT_STRFMON } FmtFamily;

/* Argument positions are one-based, matching GCC diagnostics.  A zero
 * first_vararg denotes a va_list-style function: validate a literal's
 * grammar, but do not attempt to inspect the packed arguments. */
typedef struct {
    FmtFamily family;
    u8 fmt_arg;
    u8 first_vararg;
} FmtSpec;

struct AstNode;
struct Sema;
struct WarnCtx;

/* The target-gated pre-attribute knowledge table.  Kept public so the unit
 * suite can prove every row on all five targets without pretending the
 * command-line driver can cross-compile yet. */
bool warn_format_builtin_spec(TargetSpec target, const char *name,
                              FmtSpec *out);

void warn_format_check(struct WarnCtx *w, struct Sema *s,
                       const struct AstNode *call, const FmtSpec *spec);
void warn_format_check_call(struct WarnCtx *w, struct Sema *s,
                            const struct AstNode *call);

#endif
