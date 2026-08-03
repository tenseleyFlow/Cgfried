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

#endif
