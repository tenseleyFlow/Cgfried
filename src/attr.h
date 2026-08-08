#ifndef CGF_ATTR_H
#define CGF_ATTR_H

#include "diag.h"
#include "util/base.h"

/* The ownership-annotation vocabulary is shared from parsing through IR
 * analysis.  Nodes are immutable, arena-owned, and kept in source order;
 * each layer may point at the same list because a translation unit's arena
 * outlives every analysis module derived from it. */
typedef enum CgfAttrKind {
    CGF_ATTR_RETURNS_OWNED,
    CGF_ATTR_TAKES_OWNERSHIP,
    CGF_ATTR_BORROWS,
    CGF_ATTR_RETURNS_BORROWED,
    CGF_ATTR_NO_ESCAPE,
    CGF_ATTR_COUNT
} CgfAttrKind;

typedef struct CgfAttr CgfAttr;
struct CgfAttr {
    CgfAttrKind kind;
    u32 arg; /* 1-based C source parameter; zero for cgf_returns_owned */
    /* Lowering fills this in cloned IR lists.  ABI expansion may insert a
     * hidden return pointer or split earlier aggregate arguments, so the
     * instruction operand is not always `arg - 1`. */
    u32 ir_arg; /* 1-based IR parameter/operand; zero before lowering */
    Span span;
    CgfAttr *next;
};

const char *cgf_attr_name(CgfAttrKind kind);

/* The GNU attributes this compiler IMPLEMENTS, in the form the rest of it
 * needs. Deliberately NOT a list like CgfAttr: those are memsafe ownership
 * contracts attached to parameters, while these are SYMBOL PROPERTIES, and
 * every one is a fact some backend has to emit. Flattening them here is what
 * lets a backend ask the module -- the same shape `is_tls` already uses, and
 * for the same reason: the property belongs to the object, not to any
 * reference to it.
 *
 * Which attributes may live here is not a free choice: an attribute earns a
 * field only when ignoring it would change layout, linkage or behaviour.
 * src/parse/gnu_attrs.def carries that classification. */
typedef enum {
    GNU_VIS_UNSPEC = 0, /* nothing said; ordinary ELF default binding */
    GNU_VIS_DEFAULT,
    GNU_VIS_HIDDEN,
    GNU_VIS_PROTECTED,
    GNU_VIS_INTERNAL
} GnuVisibility;

struct AstNode;

typedef struct GnuDeclAttrs {
    bool weak;
    u8 visibility; /* GnuVisibility */
    /* `packed` on a MEMBER declaration. The record-level spelling never
     * reaches here: the parser hands it straight to the record it follows,
     * because only the parser can tell a trailing attribute (which packs the
     * record) from a leading one (which gcc ignores). Anything still carrying
     * `packed` when a declaration is finished is therefore misplaced, and the
     * declaration path warns rather than silently dropping it. */
    bool packed;
    /* `aligned(N)`. The argument is a constant EXPRESSION -- gcc accepts
     * `aligned(sizeof(long))` and headers use it -- so the parser records the
     * expression and sema folds it with the same evaluator `_Alignas` uses.
     * `aligned_bare` is the no-argument form, which gcc defines as the
     * target's biggest alignment (measured: 16 on x86-64 AND arm64-linux).
     *
     * Unlike `_Alignas`, `aligned` only ever RAISES: asking for less than the
     * natural alignment is silently declined rather than being a constraint
     * violation, so `aligned(1)` is NOT a spelling of `packed`. Every consumer
     * of an align_override field already compares with `>`, which is exactly
     * that rule. */
    struct AstNode *aligned_expr;
    bool aligned_bare;
    /* Two `aligned` attributes on ONE declaration. gcc takes the maximum;
     * only one expression fits here, and quietly keeping the wrong one is the
     * failure mode docs/gnu-extensions.md exists to prevent, so sema refuses
     * by name instead. Vanishingly rare in real code. */
    bool aligned_conflict;
} GnuDeclAttrs;

/* Attributes accumulate across the specifier and declarator positions of one
 * declaration, so merging is union rather than replacement: a prefix
 * `weak` and a suffix `visibility("hidden")` on one declaration say both. */
void gnu_attrs_merge(GnuDeclAttrs *dst, const GnuDeclAttrs *src);

const char *gnu_visibility_name(u8 vis);

#endif
