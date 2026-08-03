#ifndef CGF_MEMSAFE_AUTOFIX_H
#define CGF_MEMSAFE_AUTOFIX_H

struct AstNode;
struct Preprocessor;
struct Sema;
struct WarnCtx;

/* Emits source-level memory-safety suggestions over a fully typed AST.
 * Run after sema and before IR lowering; the pass never mutates the AST or
 * source. Fix-it applicability is deliberately decided here, at the point
 * where both type proof and spelling provenance are available. */
void memsafe_autofix_translation_unit(struct WarnCtx *warn, struct Sema *sema,
                                      struct AstNode *tu,
                                      const struct Preprocessor *pp);

#endif
