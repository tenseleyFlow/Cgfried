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
 * contracts attached to parameters, while these are declaration properties.
 * Most become facts a backend emits for a symbol; `mode` and `may_alias` are
 * the exceptions that change the semantic Type graph instead.
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

/* The priority an unprioritized `constructor`/`destructor` gets, and the top
 * of the legal range. Measured, and the two facts are the same fact: gcc emits
 * the plain unnumbered `.init_array` for BOTH `constructor` and
 * `constructor(65535)`, so 65535 is not merely the maximum, it IS the default.
 * 0..100 are reserved for the implementation and warn. */
#define CGF_INIT_PRIORITY_DEFAULT 65535u
#define CGF_INIT_PRIORITY_RESERVED_MAX 100u

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
    /* `alias("target")`: this declaration DEFINES a name for a symbol defined
     * elsewhere in the same translation unit. gcc requires the target to be
     * defined here -- an alias to an undefined symbol is an error rather than
     * a linker problem -- which is what keeps it a purely local fact.
     * Interned, so it compares by pointer like every other name. */
    const char *alias_target;
    /* `used`: keep the symbol even though nothing in this translation unit
     * references it. It is a property of the OBJECT or FUNCTION, and the
     * consumer is whatever would otherwise delete it -- today that is the IPO
     * pass, which reaches only functions. Carried for globals too, so the flag
     * is already right the day global dead-stripping lands rather than being
     * remembered then. */
    bool used;
    /* `__asm__("name")` after a declarator: the SYMBOL this declaration names
     * is spelled exactly this, whatever the C identifier is. Everything the
     * linker sees uses it -- the definition's label, and every reference.
     *
     * It is a GNU extension rather than an attribute, but it belongs here for
     * the same reason `weak` does: it is a property of the symbol, decided at
     * the declaration and consumed by whatever emits or references it.
     *
     * Arena-owned rather than interned, like `alias_target`, because a string
     * literal's bytes are not interned and the parser holds no interner. */
    const char *asm_name;
    /* `constructor` / `destructor`: run this function before `main` or after
     * it returns. They are INDEPENDENT flags rather than one enum because
     * a combined `((constructor, destructor))` on one function is legal and
     * emits both entries (measured).
     *
     * The priority is a constant EXPRESSION, not a literal -- gcc folds
     * `constructor(sizeof(long) * 20)` to 160 -- so it is recorded here and
     * folded in sema by the same evaluator `aligned` uses. NULL is the
     * unprioritized form, which gcc spells as the plain `.init_array` and
     * which is also what priority 65535 produces.
     *
     * A GCC BUG WE DELIBERATELY DO NOT REPLICATE: when the attribute sits on a
     * definition that has a prior plain declaration, gcc keeps the
     * constructor-ness and SILENTLY DROPS THE PRIORITY -- stable from 8.5
     * through 16.1, and clang gets it right. gnu_attrs_merge unions like every
     * other field here, so the priority survives; docs/gnu-extensions.md
     * records the divergence. Quietly discarding an ordering the author asked
     * for is precisely the failure mode this table exists to prevent. */
    bool constructor;
    bool destructor;
    struct AstNode *ctor_priority;
    struct AstNode *dtor_priority;
    /* `cleanup(func)`: run `func(&var)` when the variable's scope exits, on
     * every ordinary path out — fall-through, `return`, `break`, `continue`
     * and a `goto` that leaves the scope. Automatic block-scope variables
     * only; anywhere else there is no scope exit to hang it on, and gcc
     * warns and drops it.
     *
     * The name is an IDENTIFIER, not an expression and not a string: gcc
     * rejects `cleanup(&f)` with its own dedicated "cleanup argument not an
     * identifier", and rejects a function POINTER variable with "cleanup
     * argument not a function". So this holds an interned identifier
     * spelling, resolved against the ordinary namespace in sema.
     *
     * A MEASURED DIVERGENCE from every other field in this struct: two
     * `cleanup` attributes on one declaration do NOT union — the LAST one
     * wins, and gcc emits no diagnostic for the one it discards. Verified by
     * execution: `((cleanup(a), cleanup(b)))` runs only `b`. Merging these
     * like `weak` would run both, which is a behaviour gcc never produces. */
    const char *cleanup_fn;
    /* `section("name")`: which output section this object or function lands
     * in. Arena-owned like the other string-valued ones.
     *
     * It is not only a directive change -- an UNINITIALIZED object in a named
     * section emits real bytes rather than going to .bss, because the section
     * the author named is where the bytes must be. gcc: `.section .s,"aw"`
     * then `.zero 4`, never a common or a .bss reservation. */
    const char *section_name;
    /* `deprecated` / `deprecated("why")`: warn at every USE of the name.
     *
     * Measured: the warning fires at the USE and never at the declaration,
     * and the DEFINITION of a deprecated function is silent.
     *
     * A CORRECTION worth keeping, because the wrong version was written
     * here first: an ENUMERATOR can carry it, and gcc warns for one. The
     * first measurement said otherwise and was reading a FILTERED slice of
     * gcc's output -- the probe put the attribute BEFORE the enumerator
     * name, where gcc rejects it outright ("expected identifier before" the
     * attribute keyword), and grepping only for `warning:` hid that error.
     * The legal position is AFTER the name: `EV attr((X)) = 1`.
     *
     * The message is a separate field rather than a defaulted string so
     * that "has the attribute" and "has a reason" stay distinct: the
     * no-message form prints `'f' is deprecated` and the other appends
     * `: why`, and an empty string is the second form, not the first. */
    bool deprecated;
    const char *deprecated_msg;
    /* `warn_unused_result`: warn when a call's value is discarded.
     *
     * THE COUNTERINTUITIVE PART, and the reason this cannot ride
     * -Wunused-value: `(void)must()` STILL WARNS in gcc. The cast suppresses
     * -Wunused-value and does NOT suppress this one -- measured. So the
     * check has to look THROUGH a cast to void rather than treating it as
     * the author's acknowledgement. */
    bool warn_unused_result;
    /* `format(archetype, string_index, first_to_check)`: check this
     * function's calls the way Sprint 39 checks printf's.
     *
     * The three fields ARE a FmtSpec, but spelled out rather than embedding
     * one, because attr.h is included by the parser and format.h belongs to
     * the warning engine -- the include arrow runs one way. Sema rebuilds
     * the spec at the one place that consumes it.
     *
     * `first_to_check` of 0 is the va_list form (`vprintf`-shaped): check
     * the literal's grammar, do not inspect packed arguments. That is not a
     * sentinel we invented -- it is what gcc's own attribute means and what
     * FmtSpec.first_vararg already encoded. */
    bool has_format;
    u8 fmt_family; /* FmtFamily, kept as u8 to avoid the include */
    u8 fmt_arg;
    u8 fmt_first_vararg;
    /* `nonnull(1,2,...)`, or bare `nonnull`.
     *
     * The BARE form is not "no positions", it is EVERY pointer parameter --
     * measured: `nonnull` on `f(int *p, int *q)` warns for a null in either
     * slot. So the two forms need separate state; a zero mask would
     * otherwise be indistinguishable from "all". Positions are 1-based to
     * match gcc's diagnostic, and the mask covers 1..64, which is past any
     * real signature. */
    bool nonnull_all;
    u64 nonnull_mask;
    /* `noreturn`: this call does not come back.
     *
     * The only one of D3's five that feeds ANALYSIS rather than emitting a
     * diagnostic of its own, and its value is measured in FALSE POSITIVES
     * REMOVED: without it, a function ending in a call to a noreturn callee
     * draws "control reaches end of non-void function", and a variable set
     * on every path that does not die draws "may be used uninitialized".
     * gcc is silent for both.
     *
     * It joins C11 `_Noreturn` and the hardcoded library-name list at ONE
     * decision in lower_call, rather than becoming a second mechanism. */
    bool noreturn;
    /* `may_alias`: accesses through this TYPE do not participate in TBAA.
     * It is useful on typedefs (`typedef int alias_int ...`) and on record
     * definitions (`struct ... S { ... }`), where member accesses inherit it.
     * The semantic Type graph carries the durable bit; this field is only the
     * parser-to-sema handoff. */
    bool may_alias;
    /* `mode(M)` / `__mode__(__M__)`: give this declaration the integer type
     * of machine mode M, keeping the DECLARED type's signedness. Measured:
     * a `typedef int r` carrying `__mode__(__word__)` is exactly `long` on
     * LP64 -- types_compatible_p says identical, not merely same-sized --
     * and the unsigned spelling gives exactly `unsigned long`. So the
     * attribute supplies the WIDTH and the declaration keeps the SIGN.
     *
     * Only the INTEGER modes are here. The 128-bit, floating and vector
     * modes are refused at parse time and stay in the refused tier: each
     * names a type this compiler does not have, and accepting one silently
     * would be a type of the wrong size.
     *
     * The parser stores the mode, not a byte count, because `word` and
     * `pointer` are target facts and the parser has no target. Sema resolves
     * both through the target layout. */
    u8 mode; /* GnuMode */
} GnuDeclAttrs;

typedef enum {
    GNU_MODE_NONE = 0,
    GNU_MODE_QI,     /* 1 byte */
    GNU_MODE_HI,     /* 2 */
    GNU_MODE_SI,     /* 4 */
    GNU_MODE_DI,     /* 8 */
    GNU_MODE_BYTE,   /* 1 */
    GNU_MODE_WORD,   /* the target's word: 8 on all five */
    GNU_MODE_POINTER /* the target's pointer width */
} GnuMode;

/* Mode name -> GnuMode, accepting gcc's four spellings (`SI`, `__SI__`,
 * and the lowercase `word`/`__word__` family). GNU_MODE_NONE for a name
 * that is not an integer mode -- which includes both the modes gcc knows
 * and refuses to give us (TI, SF, V4SI) and outright typos; the caller
 * tells them apart so the two get different diagnostics. */
GnuMode gnu_mode_from_name(const char *spelling);

/* Attributes accumulate across the specifier and declarator positions of one
 * declaration, so merging is union rather than replacement: a prefix
 * `weak` and a suffix `visibility("hidden")` on one declaration say both. */
void gnu_attrs_merge(GnuDeclAttrs *dst, const GnuDeclAttrs *src);

/* True if anything here is a property of a SYMBOL. For the one position that
 * has no symbol to hang one on — a function parameter — which is otherwise a
 * silent drop. Enumerates every field, like gnu_attrs_merge. */
bool gnu_attrs_any_symbol_property(const GnuDeclAttrs *g);

/* True if anything here changes the declaration's TYPE rather than naming a
 * property of its symbol. The split matters at positions where a type
 * property must survive even though no backend symbol exists. Individual
 * properties still follow gcc's placement rules: `mode` on a parameter is
 * refused because losing it changes the ABI, while a directly-written
 * `may_alias` there is a no-op and an attributed typedef remains effective. */
bool gnu_attrs_any_type_property(const GnuDeclAttrs *g);

const char *gnu_visibility_name(u8 vis);

#endif
