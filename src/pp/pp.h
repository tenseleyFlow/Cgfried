#ifndef CGF_PP_H
#define CGF_PP_H

#include <stdbool.h>
#include <stddef.h>

#include "diag.h"
#include "util/arena.h"
#include "util/base.h"
#include "util/buf.h"
#include "util/intern.h"
#include "util/vec.h"

/* ISO C17 translation phases 1-3: ingestion (normalization), line splicing,
 * pp-tokenization. Directives are Sprint 4, macro expansion Sprint 5,
 * _Pragma Sprint 6. */

typedef u16 FileId;                       /* 1-based; 0 invalid */
typedef struct Preprocessor Preprocessor; /* fwd: PpLexer points back */

/* Seam marker: a reachable path a future sprint finishes. Concatenates into
 * the diagnostic string; grep for LANDS_IN_SPRINT to audit all seams. */
#define LANDS_IN_SPRINT(n) " (lands in Sprint " #n ")"

/* #line remap event: physical lines >= from_line display as
 * presumed_line + (phys - from_line), in file `path`. Physical LocTable
 * entries are never touched (Sprint 3 design note). */
typedef struct {
    u32 from_line;
    u32 presumed_line;
    const char *path; /* NULL = keep the real path */
} PresumedRemap;

typedef struct SourceFile {
    FileId id;
    u32 diag_file_id;  /* the Sprint-0 diag module's id for this buffer */
    const char *path;  /* as spelled on the command line / #include */
    char *contents;    /* normalized (CRLF/CR -> LF), NUL-terminated */
    u32 size;          /* bytes, excluding the NUL */
    u32 *line_offsets; /* byte offset of each physical line start */
    u32 nlines;
    PresumedRemap *remaps; /* #line events, ascending from_line */
    u32 nremaps;
    u32 remaps_cap;
    /* File identity for #pragma once: (st_dev, st_ino) captured by fstat
     * while the file was OPEN (pathnames lie: symlinks/hardlinks must
     * dedupe, same names in different dirs must not). 0/0 for buffers. */
    u64 st_dev;
    u64 st_ino;
    /* True when phase 1 appended the ISO-required final newline. A
     * backslash immediately before a SYNTHESIZED newline is not a splice
     * (gcc keeps the backslash) — findings row F19. */
    bool synth_final_newline;
    /* Include-guard shape (Sprint 6 detector; Sprint 7 fast path):
     * non-NULL iff the whole file is #ifndef X/#define X ... #endif. */
    const char *guard_macro;
    /* Sprint 26: resolved from a system dir (-isystem or builtin), or
     * included FROM a system header. -MM and (Sprint 37) warning
     * suppression consume this. */
    bool is_system;
} SourceFile;

/* --- Location table ---------------------------------------------------
 * Every pp-token carries a SrcLoc: an id into this table, encoding EITHER a
 * physical file location OR an (expanded-from, spelled-at) pair. Expansion
 * entries exist NOW (Sprint 5 populates them in anger, Sprint 7 renders
 * backtraces): retrofitting expansion chains onto flat (file,line,col)
 * triples is agony. Ids only, everywhere — never hold entry pointers across
 * inserts; the table grows. */

typedef u32 SrcLoc;
#define SRCLOC_INVALID ((SrcLoc)0)

typedef struct {
    u32 w0; /* file: line       | expansion: spelled_at   */
    u32 w1; /* file: col        | expansion: expanded_from */
    u32 w2; /* bit0: 1 = expansion; file: file_id << 1     */
    /* Expansion entries only: WHICH macro this frame expanded. Stored as
     * name + definition loc rather than a MacroDef* because #undef and
     * redefinition after expansion are legal and real — a late diagnostic
     * must name the definition the token actually came from, not whatever
     * the table holds now. */
    const char *macro_name; /* interned; NULL for file locs */
    SrcLoc macro_def_loc;
} LocEnt;

typedef struct LocTable {
    LocEnt *ents;
    size_t len;
    size_t cap;
} LocTable;

void pp_loc_init(LocTable *t);
void pp_loc_free(LocTable *t);
SrcLoc pp_loc_file(LocTable *t, FileId f, u32 line, u32 col);
SrcLoc pp_loc_expansion(LocTable *t, SrcLoc spelled_at, SrcLoc expanded_from,
                        const char *macro_name, SrcLoc macro_def_loc);
/* Expansion frame metadata; name is NULL for file locs. */
const char *pp_loc_macro_name(const LocTable *t, SrcLoc loc);
SrcLoc pp_loc_macro_def(const LocTable *t, SrcLoc loc);
bool pp_loc_is_expansion(const LocTable *t, SrcLoc loc);
/* Walks spelled_at links to the physical spelling location. */
void pp_loc_resolve(const LocTable *t, SrcLoc loc, FileId *f, u32 *line,
                    u32 *col);
/* The macro-invocation site one level up; SRCLOC_INVALID at a file loc. */
SrcLoc pp_loc_expansion_parent(const LocTable *t, SrcLoc loc);

/* --- pp-tokens --------------------------------------------------------- */

typedef enum PpTokKind {
    PPTOK_IDENT,
    PPTOK_PPNUM,
    PPTOK_CHARCONST,
    PPTOK_STRLIT,
    PPTOK_PUNCT,
    PPTOK_HEADER_NAME,
    PPTOK_OTHER,       /* stray byte (@, `, ...): error only if it survives
                          to phase 7 — -E passes it through (gcc parity) */
    PPTOK_PLACEMARKER, /* C11 6.10.3.3 placemarker: exists only inside
                          substitution, deleted after ## processing; must
                          never escape macro.c */
    PPTOK_EOF,
} PpTokKind;

/* Punctuator identities. Digraphs map onto the SAME value as their primary
 * spelling (the token's spelling string keeps the original — stringize
 * needs it). */
typedef enum PpPunct {
    PUNCT_LBRACKET,
    PUNCT_RBRACKET,
    PUNCT_LPAREN,
    PUNCT_RPAREN,
    PUNCT_LBRACE,
    PUNCT_RBRACE,
    PUNCT_DOT,
    PUNCT_ARROW,
    PUNCT_PLUSPLUS,
    PUNCT_MINUSMINUS,
    PUNCT_AMP,
    PUNCT_STAR,
    PUNCT_PLUS,
    PUNCT_MINUS,
    PUNCT_TILDE,
    PUNCT_BANG,
    PUNCT_SLASH,
    PUNCT_PERCENT,
    PUNCT_SHL,
    PUNCT_SHR,
    PUNCT_LT,
    PUNCT_GT,
    PUNCT_LE,
    PUNCT_GE,
    PUNCT_EQEQ,
    PUNCT_NOTEQ,
    PUNCT_CARET,
    PUNCT_PIPE,
    PUNCT_AMPAMP,
    PUNCT_PIPEPIPE,
    PUNCT_QUESTION,
    PUNCT_COLON,
    PUNCT_SEMI,
    PUNCT_ELLIPSIS,
    PUNCT_ASSIGN,
    PUNCT_STAR_ASSIGN,
    PUNCT_SLASH_ASSIGN,
    PUNCT_PERCENT_ASSIGN,
    PUNCT_PLUS_ASSIGN,
    PUNCT_MINUS_ASSIGN,
    PUNCT_SHL_ASSIGN,
    PUNCT_SHR_ASSIGN,
    PUNCT_AMP_ASSIGN,
    PUNCT_CARET_ASSIGN,
    PUNCT_PIPE_ASSIGN,
    PUNCT_COMMA,
    PUNCT_HASH,
    PUNCT_HASHHASH,
} PpPunct;

#define PPTOK_F_BOL 0x01   /* first token after a LOGICAL newline */
#define PPTOK_F_SPACE 0x02 /* preceded by whitespace or a comment */

/* Prosser hide-set: immutable persistent list of interned macro names. A
 * token carrying macro X in its hideset is never expanded as X again,
 * through ALL rescans — paint lives on TOKENS, never on a global
 * "currently expanding" flag (which would wrongly block sibling
 * expansions and wrongly unblock painted tokens later). */
typedef struct HideSet {
    const char *name; /* interned */
    struct HideSet *next;
} HideSet;

HideSet *pp_hs_insert(Arena *a, HideSet *hs, const char *name);
HideSet *pp_hs_intersect(Arena *a, HideSet *x, HideSet *y);
bool pp_hs_contains(const HideSet *hs, const char *name);

typedef struct PpToken {
    const char *spelling;    /* interned, NUL-terminated, splices removed */
    struct HideSet *hideset; /* NULL until Sprint 5 */
    SrcLoc loc;
    u32 len;
    u8 kind; /* PpTokKind */
    u8 flags;
    u16 punct; /* PpPunct when kind == PPTOK_PUNCT */
} PpToken;

_Static_assert(sizeof(PpToken) <= 32, "PpToken must stay lean");

/* --- Macro table + expansion ------------------------------------------- */

/* Dynamic builtins are computed at each expansion; static predefines are
 * ordinary object-like macros defined from the "<built-in>" pseudo-file. */
typedef enum {
    MACRO_BUILTIN_NONE,
    MACRO_BUILTIN_FILE,    /* __FILE__: presumed path (honors #line) */
    MACRO_BUILTIN_LINE,    /* __LINE__: presumed line of the invocation */
    MACRO_BUILTIN_COUNTER, /* __COUNTER__: per-TU counter from 0 (gcc ext) */
} MacroBuiltinKind;

typedef struct MacroDef {
    const char *name; /* interned */
    SrcLoc loc;       /* definition site */
    bool is_function;
    bool is_variadic;
    bool is_builtin; /* predefined (redefinition warns instead of erroring) */
    u8 builtin_kind; /* MacroBuiltinKind */
    u16 nparams;
    const char **params; /* interned names */
    PpToken *body;
    u32 body_len;
} MacroDef;

/* --- Conditional stack -------------------------------------------------- */

typedef struct PpCond {
    SrcLoc loc; /* the opening #if/#ifdef/#ifndef */
    bool parent_live;
    bool live;      /* this group's tokens are being consumed */
    bool taken_any; /* some group of this conditional already ran */
    bool seen_else;
} PpCond;

typedef struct PpLexer {
    Preprocessor *pp;
    SourceFile *sf;
    size_t pos;
    size_t warned_upto; /* splice-warning watermark (peeks recross bytes) */
    Buf scratch;        /* token spelling accumulator (splices removed) */
    bool at_bol;        /* logical beginning-of-line state */
    bool pending_space;
    bool had_error;
} PpLexer;

/* --- Include stack frame ------------------------------------------------ */

#define PP_INCLUDE_DEPTH_MAX 200

/* Guard-shape detector states (deliverable 6; no behavior change yet). */
typedef enum {
    GUARD_EXPECT_IFNDEF, /* nothing seen yet */
    GUARD_EXPECT_DEFINE, /* #ifndef X seen; next directive must define X */
    GUARD_IN_BODY,       /* guard open; anything inside is fine */
    GUARD_AFTER_ENDIF,   /* guard closed; ANY further token disqualifies */
    GUARD_DISQUALIFIED,
} GuardState;

typedef struct PpFrame {
    PpLexer lx;
    size_t cond_base; /* conditional-stack depth on entry: conditionals may
                         not straddle include boundaries */
    int found_dir;    /* search-chain index this file was found at
                         (#include_next resumes after it); -1 = main file */
    GuardState guard_state;
    const char *guard_macro; /* candidate (interned) */
    size_t guard_cond;       /* cond-stack index of the guard conditional */
} PpFrame;

/* --- STDC pragma state (recorded now; consumed Sprints 15/36) ----------- */

typedef enum { PP_STDC_DEFAULT, PP_STDC_ON, PP_STDC_OFF } PpStdcSwitch;

typedef struct {
    PpStdcSwitch fp_contract;
    PpStdcSwitch fenv_access;
    PpStdcSwitch cx_limited_range;
} PpStdcPragmaState;

/* --- Language standard (drives predefines and GNU-ext acceptance) ------ */

typedef enum {
    STD_C89,
    STD_C99,
    STD_C11,
    STD_C17, /* the default (locked decision) */
    STD_GNU89,
    STD_GNU99,
    STD_GNU11,
    STD_GNU17,
} CStd;

/* --- Macro-expansion output buffers (rescan stack) ---------------------
 * Expansion output is PUSHED BACK onto this stack above the file lexer, so
 * rescanning continues into the remaining input — a function-like
 * invocation can assemble across the expansion edge (ISO 6.10.3.4). */
typedef struct PpTokBuf {
    PpToken *toks;
    u32 n;
    u32 pos;
} PpTokBuf;

/* --- Preprocessor state ------------------------------------------------ */

#define PP_MAX_DIRS 32

typedef struct Preprocessor {
    Arena *arena;
    DiagCtx *diag;
    Interner *interner;
    LocTable loc;
    SourceFile **files; /* index = FileId - 1 */
    size_t nfiles;
    size_t files_cap;
    bool trigraphs;
    /* -ffreestanding: __STDC_HOSTED__ becomes 0 (C17 4p6). The driver
        sets it; the predefine block is the only consumer. */
    bool freestanding; /* -trigraphs (gcc parity: default off) */
    bool verbose;      /* -v: print the include search list */

    /* Include search chains (set up by the driver before pp_begin). */
    const char *iquote_dirs[PP_MAX_DIRS];
    size_t n_iquote;
    const char *include_dirs[PP_MAX_DIRS]; /* -I */
    size_t n_include;
    const char
        *system_dirs[PP_MAX_DIRS]; /* from TargetSpec; -nostdinc empties */
    size_t n_system;

    Strmap macros; /* interned name -> MacroDef* */

    PpFrame *frames; /* include stack; frames[nframes-1] is active */
    size_t nframes;
    size_t frames_cap;

    PpCond *conds;
    size_t nconds;
    size_t conds_cap;

    PpStdcPragmaState stdc;
    bool fatal; /* missing include etc.: drain to EOF immediately */

    /* #pragma once identities (dev,ino pairs), linear (includes are few).
     * PER-TU state: a future `-c a.c b.c` driver resets this per TU. */
    struct {
        u64 dev, ino;
    } *once;
    size_t nonce;
    size_t once_cap;

    /* Include-guard fast path cache. Guard SHAPE is a pure file property
     * (safe to keep per-process when multi-TU driving arrives); whether
     * the guard macro is currently defined is per-TU and is re-checked at
     * every include — #undef between inclusions must re-include. */
    struct {
        u64 dev, ino;
        const char *guard_macro;
        const char *path; /* as resolved; enables a syscall-free shortcut */
    } *fcache;
    size_t nfcache;
    size_t fcache_cap;
    bool guard_fastpath; /* CGF_PP_GUARD_FASTPATH=0 disables (control) */

    /* CGF_PP_STATS=1 counters — tests read these instead of timing. */
    bool stats;
    u32 inc_opened;
    u32 inc_guard_skipped;
    u32 inc_once_skipped;

    /* Pending -E passthrough tokens (#pragma lines). */
    PpToken *pass;
    u32 npass;
    u32 pass_pos;

    /* Language standard. */
    CStd std;
    bool gnu_mode;

    /* Expansion machinery. */
    PpTokBuf *bufs; /* rescan stack; bufs[nbufs-1] is consumed first */
    size_t nbufs;
    size_t bufs_cap;
    PpToken pending; /* one-token lookahead (fn-like `(` probe) */
    bool has_pending;
    bool pending_from_file; /* pending came from the lexer: may be a
                               directive `#`; buffer tokens never are */
    u32 counter;            /* __COUNTER__ */
    bool collecting_args;   /* directive-in-argument-list detection */
    bool in_if_line;        /* #if expansion: `defined` operands stay raw */
} Preprocessor;

void pp_init(Preprocessor *pp, Arena *arena, DiagCtx *diag, Interner *interner);
/* Registers an in-memory buffer (takes a copy; used by tests and stdin). */
SourceFile *pp_source_add_buffer(Preprocessor *pp, const char *path,
                                 const char *bytes, size_t len);
/* Loads + normalizes a file; NULL + diagnostic on I/O error (exit 3). */
SourceFile *pp_source_load(Preprocessor *pp, const char *path);

/* The Span a diagnostic at `loc` would carry (physical + #line presumed
 * display info). Consumers past the preprocessor (the lexer onward) use
 * this so they never synthesize locations. */
Span pp_span(Preprocessor *pp, SrcLoc loc, u32 len);

/* Diagnostic at a SrcLoc; anything inside a macro expansion automatically
 * gets its expansion backtrace (Sprint 7). Front-end phases route their
 * diagnostics through this to inherit that. */
void pp_diag_at(Preprocessor *pp, DiagLevel lvl, SrcLoc loc, u32 len,
                const char *fmt, ...);

/* --- pp-lexer (phases 2+3 fused: splices are skipped, never rewritten,
 * so every token keeps its true physical location) -------------------- */

void pp_lexer_init(PpLexer *lx, Preprocessor *pp, SourceFile *sf);
/* False at EOF (out is then the PPTOK_EOF token). */
bool pp_lex_token(PpLexer *lx, PpToken *out);
/* ONLY valid in #include context (the directive engine calls it);
 * header-names do not exist elsewhere — never called from the main loop. */
bool pp_lex_header_name(PpLexer *lx, PpToken *out);
/* Non-consuming: is the next token on a new logical line (or EOF)? */
bool pp_lex_at_line_end(PpLexer *lx);

const char *pp_tok_kind_name(PpTokKind k);

/* --- Engine (directive.c): the token stream the rest of the compiler
 * consumes. Directives are handled internally; pp_next yields text tokens
 * only (plus passed-through #pragma lines under -E). ------------------- */

/* Pushes the main file (and, first, the <command-line> -D/-U buffer if
 * cmdline is non-NULL). */
void pp_begin(Preprocessor *pp, SourceFile *main_file, SourceFile *cmdline);
bool pp_next(Preprocessor *pp, PpToken *out); /* false at end of input */
void pp_end(Preprocessor *pp);                /* frees engine-owned buffers */

/* --- Macro table (macro.c) --------------------------------------------- */

/* Parses a #define line (tokens after the `define` keyword). */
void pp_macro_define_line(Preprocessor *pp, const PpToken *toks, u32 n);
void pp_macro_undef(Preprocessor *pp, const char *name, SrcLoc loc);
const MacroDef *pp_macro_lookup(const Preprocessor *pp, const char *name);
/* As `defined`/#ifdef see it: macros PLUS _Pragma (gcc+clang parity). */
bool pp_name_is_defined(const Preprocessor *pp, const char *name);

/* One collected macro argument: raw tokens always; the pre-expanded form
 * computed LAZILY (an argument used only as a #/## operand must never be
 * expanded, even if expanding it would error). */
typedef struct MacroArg {
    const PpToken *raw;
    u32 nraw;
    PpToken *expanded;
    u32 nexp;
    bool computed;
} MacroArg;

/* Substitution core (Prosser): builds the fully-substituted, pasted,
 * placemarker-free replacement list; every output token gets hideset
 * hs_new merged in and an expansion SrcLoc chaining to `inv`. */
PpToken *pp_macro_subst(Preprocessor *pp, const MacroDef *m, MacroArg *args,
                        u32 nargs, HideSet *hs_new, SrcLoc inv, u32 *out_n);
bool pp_macro_check_args(Preprocessor *pp, const MacroDef *m, u32 nargs,
                         SrcLoc loc);
PpToken pp_builtin_token(Preprocessor *pp, const MacroDef *m, SrcLoc loc);

/* Registers predefined macros for the configured std/target: static ones
 * via the returned "<built-in>" pseudo-file (process before everything),
 * dynamic builtins (__FILE__/__LINE__/__COUNTER__) directly in the table.
 * __DATE__/__TIME__ freeze once (SOURCE_DATE_EPOCH honored). */
SourceFile *pp_predefine_all(Preprocessor *pp);

/* Isolated macro expansion of a token list (#if lines, computed includes,
 * #line arguments, argument pre-expansion). No cross-boundary rescan: a
 * function-like name whose `(` is not inside the list stays unexpanded. */
u32 pp_expand_list(Preprocessor *pp, const PpToken *in, u32 n, PpToken **out);

/* Stream-side expansion attempt for one identifier token: returns true if
 * it consumed the token (expansion output pushed onto the rescan stack, or
 * a dynamic builtin was synthesized). False: emit the token as-is. */
bool pp_try_expand(Preprocessor *pp, const PpToken *t);

/* Dumps #define lines for -dM (dynamic builtins omitted, gcc parity). */
void pp_dump_macros(Preprocessor *pp);

/* -E writer support: must a space be inserted between adjacently-printed
 * tokens to keep them two tokens? */
bool pp_tokens_would_merge(Preprocessor *pp, const PpToken *a,
                           const PpToken *b);

/* --- PP constant expressions (ppexpr.c) -------------------------------- */

/* Full pipeline for a #if/#elif token line: defined-replacement, expansion
 * seam, evaluation. False on error (diagnosed) or a zero result. */
bool pp_eval_condition(Preprocessor *pp, const PpToken *toks, u32 n,
                       SrcLoc loc);

#endif
