#ifndef CGF_SEMA_WARN_EXPR_H
#define CGF_SEMA_WARN_EXPR_H

#include "sema/sema.h"

/* Context supplied by the statement walker.  The checker is recursive:
 * callers invoke it once for a complete, already-typed expression. */
typedef enum SemaWarnExprContext {
    SEMA_WARN_EXPR_VALUE = 0,
    SEMA_WARN_EXPR_DISCARDED = 1u << 0,
    SEMA_WARN_EXPR_TRUTH = 1u << 1
} SemaWarnExprContext;

void sema_warn_expr(Sema *s, AstNode *expr, unsigned context);

/* Check an assignment-like conversion after conv_assignable has rewritten
 * the RHS.  `converted` is normally an implicit AST_EXPR_CAST; explicit
 * casts in the source are deliberately treated as an opt-out. */
void sema_warn_implicit_conversion(Sema *s, Type *destination,
                                   AstNode *converted);

#endif
