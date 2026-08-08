#ifndef CGF_SEMA_H
#define CGF_SEMA_H

#include "diag.h"
#include "lex/lex.h"
#include "parse/ast.h"
#include "target.h"
#include "util/arena.h"
#include "util/base.h"
#include "util/intern.h"
#include "util/softfp.h"

/* The semantic core: the Type graph, the scope stack over C's four
 * namespaces, linkage, and redeclaration merging.
 *
 * NOTHING here needs a byte size. Sizes and layout are Sprint 14,
 * conversions Sprint 13, constant evaluation Sprint 15; every path that
 * would need one hard-errors naming its sprint rather than guessing. */

typedef enum {
    TY_VOID,
    TY_BOOL,
    TY_CHAR,
    TY_SCHAR,
    TY_UCHAR,
    TY_SHORT,
    TY_USHORT,
    TY_INT,
    TY_UINT,
    TY_LONG,
    TY_ULONG,
    TY_LLONG,
    TY_ULLONG,
    TY_FLOAT,
    TY_DOUBLE,
    TY_LDOUBLE,
    TY_PTR,
    TY_ARRAY,
    TY_FUNC,
    TY_STRUCT,
    TY_UNION,
    TY_ENUM,
    TY_ERROR /* poisoned: never diagnosed about again (Sprint 11 contract) */
} TypeKind;

#define CGF_QUAL_CONST 0x1u
#define CGF_QUAL_VOLATILE 0x2u
#define CGF_QUAL_RESTRICT 0x4u
#define CGF_QUAL_ATOMIC 0x8u

typedef struct Type Type;
typedef struct TagDecl TagDecl;
typedef struct Member Member;
typedef struct Symbol Symbol;

struct Member {
    const char *name; /* interned; NULL for an anonymous member */
    Type *type;
    AstNode *bitfield_width; /* the EXPRESSION; Sprint 15 evaluates it */
    bool is_bitfield;
    Span span;

    /* Filled by layout_record (Sprint 14). `offset` is in BYTES from the
     * start of the record; for a bitfield, `bit_offset` is the bit
     * position within the whole record and `bit_width` its declared
     * width. `container_size` is the declared type's size — AAPCS64
     * mandates that a volatile bitfield access use exactly the container
     * width, so Sprints 20 and 48 read it rather than re-deriving it. */
    u64 offset;
    u64 bit_offset;
    u32 bit_width;
    u64 container_size;
    u64 align_override; /* _Alignas on the member, 0 = none */
    /* `packed`, from the member's own attribute OR from the record's. Layout
     * reads exactly this field, so member-level and record-level packing are
     * the same rule applied to a different set of members. */
    bool packed;
    bool laid_out;

    Member *next;
};

struct TagDecl {
    const char *name; /* interned; NULL when anonymous */
    /* Layout is memoized per tag: it is a pure function of the members
     * and the target, and a deep struct tree would otherwise be walked
     * once per sizeof. `laid_out_for` guards against a memo computed for
     * a DIFFERENT target leaking into a cross-compile. */
    bool laid_out;
    TargetKind laid_out_for;
    u64 size;
    u64 align;
    TypeKind kind; /* TY_STRUCT / TY_UNION / TY_ENUM */
    bool complete;
    Member *members;
    u32 nmembers;
    Type *enum_underlying; /* TY_ENUM only, chosen per gcc's ladder */
    AstNode *enum_ast;     /* originating AST_ENUM_DECL; switch warnings use
                              its declaration-ordered enumerator list */
    bool has_fam;          /* last member is a flexible array (6.7.2.1p18) */
    bool defining;         /* completion in progress: a nested definition of
                              the same tag is a "nested redefinition", not a
                              completion of OURSELVES — without this check a
                              self-referential member cycle forms and every
                              later member walk recurses forever (found by
                              the fuzzer, seed 1773) */
    u64 align_override;    /* _Alignas on the record, 0 = none */
    bool packed;           /* the record carries `packed`: every member is
                              placed at alignment 1 and the record's own
                              alignment drops to 1 with it -- forgetting the
                              second half gives right offsets and wrong
                              sizeof (.docs/audits/packed-layout.md) */
    Span span;
    Type *type; /* the one Type node that names this tag */
};

struct Type {
    TypeKind kind;
    unsigned quals;

    Type *base; /* TY_PTR pointee / TY_ARRAY element / TY_FUNC return */

    /* TY_ARRAY. The SIZE is only present once Sprint 15 can evaluate the
     * bound; `has_size` false means an incomplete array type. */
    bool has_size;
    u64 size;
    bool is_vla;
    AstNode *size_expr;

    /* TY_FUNC. `has_proto` false is the K&R / `f()` unspecified form,
     * which is compatible with anything on the parameter side. */
    Type **params;
    u32 nparams;
    bool variadic;
    bool has_proto;

    TagDecl *tag; /* TY_STRUCT / TY_UNION / TY_ENUM */
};

/* C11 6.2.3: FOUR namespaces. Ordinary holds objects, functions, typedefs
 * and enum CONSTANTS in one shared space; tags hold struct, union AND enum
 * together (so `struct T` and `enum T` collide in one scope); members are
 * per-struct; labels are function-scoped and live on the parser side. */
typedef enum { NS_ORDINARY, NS_TAG, NS_LABEL } Namespace;

typedef enum { LINK_NONE, LINK_INTERNAL, LINK_EXTERNAL } Linkage;

/* The C99 inline decision, computed at END of translation unit from every
 * declaration of the function (6.7.4p7). Sprint 19 reads it to decide
 * whether this TU emits the external definition; Sprint 33 reads it only
 * as an inliner hint, because `inline` is a hint and not a command. */
typedef enum {
    INL_NONE,         /* not inline */
    INL_STATIC,       /* static inline: internal, emit if referenced */
    INL_INLINE_DEF,   /* inline definition: do NOT emit externally */
    INL_EXTERN_INLINE /* THIS TU provides the external definition */
} InlineKind;

/* How a file-scope object resolves at end of TU, for Sprint 19/24 symbol
 * emission. gcc 8's default is -fcommon, so external tentatives become
 * COMMON symbols and multiple TUs each saying `int x;` link. */
typedef enum {
    DEF_NONE,      /* declaration only (extern, no definition here) */
    DEF_COMMON,    /* external tentative under -fcommon */
    DEF_ZERO_INIT, /* tentative resolved to a zero-initialized definition */
    DEF_INIT       /* an explicit initializer */
} DefKind;

typedef enum {
    SYM_VAR,
    SYM_FUNC,
    SYM_TYPEDEF,
    SYM_ENUM_CONST,
    SYM_TAG
} SymKind;

struct Symbol {
    const char *name; /* interned: compared by pointer */
    SymKind kind;
    Namespace ns;
    Type *type;
    Linkage linkage;
    Span span;

    bool tentative; /* file-scope object, no initializer (6.9.2p2) */
    bool defined;   /* has an initializer, or is a function definition */
    bool is_param;
    bool static_storage; /* object address is a link-time constant */
    u32 reads;  /* value/address uses; a plain assignment lhs is corrected */
    u32 writes; /* assignments after declaration; initializer is not one */
    bool tls;   /* _Thread_local */
    /* _Alignas on the OBJECT, 0 = none. Merged across declarations by MAX,
     * which is what makes a raise legal and a weakening impossible. Members
     * have had this since Sprint 14 as Member.align_override; objects had
     * their alignment VALIDATED and then discarded, so `_Alignas(64) int g`
     * emitted `.p2align 2` and the address was not 64-aligned at run time. */
    u64 align_override;
    /* Implemented GNU attributes. Merged across declarations like the
     * inline matrix is: a plain declaration followed by a `weak` one
     * says weak, because the two declarations are one symbol. */
    GnuDeclAttrs gnu;
    bool is_main;
    u32 func_specs; /* AST_FS_*, ORed across declarations */
    /* Inline bookkeeping (6.7.4p7): the decision needs EVERY declaration,
     * so these accumulate and sema_finish computes inline_kind. */
    bool all_decls_inline; /* every declaration so far had `inline` */
    bool any_decl_extern;  /* any declaration had `extern` */
    u8 inline_kind;        /* InlineKind, valid after sema_finish */
    u8 def_kind;           /* DefKind, valid after sema_finish */
    i64 enum_value;        /* SYM_ENUM_CONST */
    TagDecl *tag;          /* SYM_TAG */
    CgfAttr *cgf_attrs;    /* merged ownership contract, source-ordered */

    Symbol *next; /* intrusive chain within one scope, newest first */
};

typedef enum {
    SCOPE_FILE,
    SCOPE_BLOCK,
    SCOPE_PROTO, /* a parameter list: everything declared here dies at ')' */
    SCOPE_FUNC
} ScopeKind;

typedef struct Scope Scope;
struct Scope {
    ScopeKind kind;
    /* Intrusive lists, newest first, so a shadowing declaration is simply
     * found first. Arena-allocated — src/sema/ makes no heap allocation of
     * its own, which is what keeps the whole pass leak-free by
     * construction rather than by discipline. */
    Symbol *ordinary;
    Symbol *tags;
    Scope *parent;
};

typedef struct Sema {
    Arena *arena;
    DiagCtx *dc;
    Interner *interner;
    const LangOpts *lang;
    TargetSpec target;
    Scope *scope;
    Scope *file_scope;
    u32 nerrors;
    bool dump;    /* -fdump-sema */
    bool fcommon; /* -fcommon (gcc-8's default) vs -fno-common */
    /* Function-body state: the return checks need to know whose body this
     * is; reset per definition. */
    Type *cur_ret;
    const char *cur_fname;
    u32 cur_func_specs;
    /* The VM-scope jump checker (6.8.6.1p1): a goto or switch may not
     * jump INTO the scope of a variably modified object, bypassing its
     * size evaluation. The chain mirrors the lexical scope stack; labels
     * and gotos snapshot it, and jumps resolve at end of function. */
    struct VmDecl *vm_chain;
    struct VmLabel *vm_labels;
    struct VmGoto *vm_gotos;
    struct VmDecl *vm_switch_chain;
    bool in_switch;
    /* True while walking the body of a candidate INLINE DEFINITION (all
     * declarations so far inline, none extern, external linkage): 6.7.4p3
     * forbids it defining modifiable static objects or referencing
     * internal linkage — gcc warns; observed and matched. */
    bool cur_inline_candidate;
    /* The synthesized __builtin_va_list type (Sprint 19): array[1] of
     * the SysV record — built on first use, one per TU. */
    Type *va_list_type;
} Sema;

typedef struct VmDecl {
    const char *name;
    Span span;
    struct VmDecl *parent;
} VmDecl;

typedef struct VmLabel {
    const char *name;
    VmDecl *chain;
    struct VmLabel *next;
} VmLabel;

typedef struct VmGoto {
    const char *label;
    Span span;
    VmDecl *chain;
    struct VmGoto *next;
} VmGoto;

/* --- types --------------------------------------------------------------- */

/* Interned: one canonical node per basic kind, so basics compare by
 * pointer. Derived types (pointer/array/function/tagged) are allocated per
 * use and compared STRUCTURALLY — globally uniquing them is a trap,
 * because the same tag completes differently per translation unit and
 * qualifier combinations multiply nodes regardless. */
Type *type_basic(TypeKind k);
Type *type_qualify(Arena *ar, const Type *t, unsigned quals);
Type *type_ptr(Arena *ar, Type *pointee);
Type *type_array(Arena *ar, Type *elem);
Type *type_func(Arena *ar, Type *ret);
Type *type_tag(Arena *ar, TagDecl *tag);

bool type_is_basic(const Type *t);
bool type_is_integer(const Type *t);
bool type_is_arithmetic(const Type *t);
bool type_is_complete(const Type *t);
/* C11 6.2.7 compatibility. Basic types are compatible only when their
 * kinds are IDENTICAL: `int` and `long` are incompatible even where they
 * have the same width, and `char`/`signed char`/`unsigned char` are three
 * distinct types (6.2.5p15) even where char is signed. */
bool type_compatible(const Type *a, const Type *b);
/* 6.2.7p3. `int a[]` + `int a[10]` -> `int[10]`; `int f()` + a prototype
 * -> the prototype. Requires the two to be compatible. */
Type *type_composite(Arena *ar, Type *a, Type *b);
char *type_to_str(Arena *ar, const Type *t);

/* Cross-TU struct compatibility is member-wise (6.2.7p1) and only
 * observable at link time; Sprint 57 owns it. */
bool type_compatible_cross_tu(Sema *s, const Type *a, const Type *b);

/* --- constant expressions (Sprint 15) ------------------------------------ */

/* Which softfloat format the target uses for a floating type. `long
 * double` is the cross-target trap: x87 80-bit on x86-64, IEEE binary128
 * on arm64-linux, plain double on arm64-macos. */
SfFormat constexpr_format_of(Sema *s, const Type *t);
/* Parses a C floating-constant SPELLING correctly-rounded into `f`. The
 * literal grammar lives on this side because softfp.c must stay
 * library-clean for Sprint 49's runtime. */
Sf constexpr_parse_float(const char *spelling, SfFormat f, SfStatus *st);
/* The value of a float literal token, with gcc's range diagnostics. */
Sf constexpr_float_literal(Sema *s, AstNode *e);

/* C11 6.6 has THREE constant-expression categories with different rules,
 * and one permissive folder that gcc applies opportunistically. They are
 * one function with four modes rather than four functions, because four
 * functions drift apart and the drift is invisible until a program is
 * accepted in one context and rejected in another. */
typedef enum {
    CE_ICE,   /* integer constant expression: the strict constraint check */
    CE_ARITH, /* arithmetic constant expression: floats allowed */
    CE_ADDR,  /* address constant: &object + offset */
    CE_FOLD   /* opportunistic; failure is silent */
} CeMode;

typedef enum { CV_INT, CV_FLOAT, CV_ADDR, CV_ERROR } CvKind;

typedef struct {
    CvKind kind;
    Type *type;
    u64 i;       /* CV_INT: the two's-complement bit pattern, target width */
    Sf f;        /* CV_FLOAT: in the format its type calls for */
    Symbol *sym; /* CV_ADDR: the symbol, for Sprint 19's relocation */
    i64 addend;
    /* CV_ADDR with sym == NULL: the ANONYMOUS object the address names —
     * an AST_EXPR_STRING or a file-scope AST_EXPR_COMPOUND_LIT. Sprint
     * 18's lowering materializes it as an internal global; carrying the
     * node is what lets it know WHICH one. */
    const AstNode *anon;
} ConstValue;

ConstValue constexpr_eval(Sema *s, AstNode *e, CeMode mode);
/* The ICE entry point every constraint context uses. `what` names the
 * context in the diagnostic ("array bound", "case label", ...). */
bool sema_require_ice(Sema *s, AstNode *e, i64 *out, const char *what);

/* --- static initializer images (consumed by Sprint 19) ------------------- */

typedef struct {
    u64 offset;  /* byte offset within the image */
    Symbol *sym; /* the symbol whose address goes here; NULL when the
                    address names an anonymous object (see `anon`) */
    i64 addend;
    const AstNode *anon; /* sym == NULL: the string literal / file-scope
                            compound literal to materialize (Sprint 18) */
} InitReloc;

typedef struct {
    u8 *bytes;
    u64 size;
    InitReloc *relocs;
    u32 nrelocs;
} InitImage;

/* Evaluates a static initializer into the exact bytes Sprint 19 will emit
 * into .data/.rodata, plus the relocations for any address constants.
 * Padding is ZERO — both for determinism and to match gcc. */
bool constexpr_eval_initializer(Sema *s, Type *type, AstNode *init,
                                InitImage *out);
/* -fdump-init: one line per file-scope object with an initializer, as
 * hex bytes plus any relocations. This is exactly what Sprint 19 emits. */
void constexpr_dump_initializers(Sema *s, AstNode *tu, FILE *f);

/* --- layout (Sprint 14) -------------------------------------------------- */

typedef struct {
    u64 size;
    u64 align;
} TypeLayout;

/* Size and alignment of a COMPLETE type. On an incomplete one this is a
 * programming error rather than a 0 size — callers check completeness and
 * diagnose at their own site, where they can say which construct demanded
 * the size. */
TypeLayout layout_of(Sema *s, Type *t);
bool layout_is_complete_for_size(const Type *t);
/* Lays out a record, filling every Member's offset/bit_offset/bit_width.
 * Idempotent and memoized on the TagDecl. */
void layout_record(Sema *s, Type *rec);
u64 layout_offsetof(Sema *s, Type *rec, const Member *m);

/* SysV x86-64 parameter classification (psABI 3.2.3). Consumed by Sprint
 * 19's call lowering and Sprint 23's x86 calls. */
typedef enum {
    ABI_NO_CLASS,
    ABI_INTEGER,
    ABI_SSE,
    ABI_SSEUP,
    ABI_X87,
    ABI_X87UP,
    ABI_COMPLEX_X87,
    ABI_MEMORY
} AbiClass;

/* Returns the eightbyte count (1 or 2), or -1 for MEMORY. */
int layout_classify_sysv(Sema *s, Type *t, AbiClass out[2]);

/* AAPCS64 homogeneous float aggregate: up to 4 members, all the same
 * floating type. The PREDICATE lands now so Sprint 19 can shape calls
 * target-parameterized once; the codegen that consumes it is Sprint 48. */
bool layout_is_hfa(Sema *s, Type *t, Type **base, int *count);

/* -fdump-layout: one line per record and member (offset, bit offset,
 * width, size, align). Sprint 19 reuses this format. */
void layout_dump(Sema *s, FILE *f);

/* --- conversions (6.3) --------------------------------------------------- */

/* Which construct is doing the assigning. It exists so ONE function
 * produces gcc's four different sentences ("assignment to X from Y",
 * "passing argument N of 'f'", ...) rather than four near-copies drifting
 * apart. */
typedef enum { ACTX_ASSIGN, ACTX_INIT, ACTX_ARG, ACTX_RETURN } AssignCtxKind;

typedef struct {
    AssignCtxKind kind;
    u32 arg_index;      /* ACTX_ARG: 1-based, as gcc numbers them */
    const char *callee; /* ACTX_ARG: the function's name, if known */
} AssignCtx;

/* Implicit conversions are MATERIALIZED as AST_EXPR_CAST nodes with
 * `implicit` set. Later passes read the tree; none re-derives the rules. */
AstNode *conv_cast(Sema *s, AstNode *e, Type *to);
AstNode *conv_lvalue(Sema *s, AstNode *e);  /* drops TOP-level quals */
AstNode *conv_decay(Sema *s, AstNode *e);   /* array->ptr, func->ptr */
AstNode *conv_promote(Sema *s, AstNode *e); /* integer promotions */
AstNode *conv_to_bool(Sema *s, AstNode *e); /* `!= 0`, never truncation */
Type *conv_uac(Sema *s, AstNode **a, AstNode **b);
/* The type-level halves, so the unit suite can build a truth table
 * without constructing expressions. */
Type *conv_promote_type(Sema *s, Type *t);
Type *conv_uac_type(Sema *s, Type *a, Type *b);
Type *conv_strip_quals(Sema *s, const Type *t);
int conv_rank(const Type *t);
bool conv_is_signed(Sema *s, const Type *t);
u32 conv_int_bits(Sema *s, const Type *t);
/* 6.3.2.3p3. `(char *)0` is a null POINTER but not a null pointer
 * CONSTANT — the difference decides `?:` typing and varargs sentinels. */
bool conv_is_npc(Sema *s, const AstNode *e);
bool conv_assignable(Sema *s, Type *lhs, AstNode **rhs_slot, AssignCtx ctx);

/* --- expressions --------------------------------------------------------- */

/* Types an expression in place: fills sem_type/is_lvalue and rewrites
 * children with the implicit conversions the operator demands. Returns
 * the (possibly wrapped) node. */
AstNode *sema_expr(Sema *s, AstNode *e);
/* Return the declared object at the root of a direct/member/array lvalue;
 * pointer dereferences and pointer-based subscripts deliberately have none. */
AstNode *sema_lvalue_root_ident(AstNode *e);

/* --- scopes -------------------------------------------------------------- */

void sema_init(Sema *s, Arena *ar, DiagCtx *dc, Interner *in,
               const LangOpts *lang, TargetSpec target);
Scope *scope_push(Sema *s, ScopeKind k);
void scope_pop(Sema *s);
Symbol *scope_lookup(Scope *sc, const char *name, Namespace ns);
/* Lookup restricted to ONE scope — the redeclaration check, and what
 * separates "redeclared here" from "shadows an outer declaration". */
Symbol *scope_lookup_local(Scope *sc, const char *name, Namespace ns);
Symbol *scope_declare(Sema *s, Symbol *sym);
Symbol *sym_new(Sema *s, const char *name, SymKind kind, Namespace ns,
                Type *type, Span span);

/* --- the pass ------------------------------------------------------------ */

void sema_run(Sema *s, AstNode *tu);
/* Sprint 46's -fsafe restrictions are a post-typing policy pass. Keeping
 * them separate from ISO C semantic analysis guarantees that rejected
 * programs are never silently reinterpreted. */
void sema_check_safe_mode(Sema *s, AstNode *tu);
/* End-of-TU resolution: the inline matrix and tentative definitions.
 * Called by sema_run; exposed for the unit suite. */
void sema_finish(Sema *s);
/* -fdump-sema: file-scope symbols with their resolved types and linkage,
 * in DECLARATION order. */
void sema_dump(Sema *s, FILE *f);
/* Teaches ast.c to render semantic types; call once before any dump. */
void sema_install_renderer(void);
/* Converts an AST type chain to a semantic Type. Exposed for unit tests. */
Type *sema_type_from_ast(Sema *s, const AstType *at, Span span);

/* The __builtin_va_list type: `struct { unsigned gp_offset, fp_offset;
 * void *overflow_arg_area, *reg_save_area; }[1]` — an ARRAY type, so it
 * decays when passed (the classic va_list portability trap, on purpose:
 * matching the psABI exactly is what makes our va_list interoperable). */
Type *sema_va_list_type(Sema *s);

/* Builtin markers sema leaves on an AST_EXPR_CALL's `op` field for
 * lowering (ordinary calls leave op zero). Numbered from the table in
 * builtins.def so adding a row cannot collide or leave a hole. */
enum {
    SEMA_BUILTIN_NONE = 0,
    SEMA_BUILTIN_PAD = 0x7000, /* so the first table row lands on 0x7001 */
#define B(sfx, NAME, nargs, kind) SEMA_BUILTIN_##NAME,
#include "builtins.def"
#undef B
    SEMA_BUILTIN_LAST
};
#define SEMA_BUILTIN_FIRST 0x7001

/* Result-type rules (see builtins.def's header comment). */
typedef enum {
    BK_VOID,
    BK_INT,
    BK_LONG,
    BK_SIZE,
    BK_VOIDP,
    BK_DOUBLE,
    BK_FLOAT,
    BK_ARG0,
    BK_SPECIAL
} BuiltinKind;

/* Table lookup by spelling AFTER the "__builtin_" prefix. Returns the
 * marker (0 when the name is not a builtin we implement) and fills the
 * expected argument count and result rule. */
u16 sema_builtin_lookup(const char *suffix, int *nargs, int *kind);

/* True when `name` is reachable as a member of `t`, traversing
 * anonymous struct/union members (6.7.2.1p13). */
bool find_member_named(const Type *t, const char *name);

/* Every deferred path routes through here so the grep in the sprint's DoD
 * finds them all: the message ALWAYS names the sprint that lands it. */
void sema_unimplemented(Sema *s, Span span, const char *what, int sprint);

#endif
