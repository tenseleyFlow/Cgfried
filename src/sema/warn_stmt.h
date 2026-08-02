#ifndef CGF_SEMA_WARN_STMT_H
#define CGF_SEMA_WARN_STMT_H

#include "sema/sema.h"

struct Preprocessor;

/* Sprint 38's post-typing warning walk.  It deliberately runs after
 * sema_run: expression conversions, enum values, and declaration symbols
 * must already be attached to the syntax tree before warning policy looks
 * at them. */
void sema_warn_translation_unit(Sema *s, AstNode *tu, struct Preprocessor *pp);

#endif
