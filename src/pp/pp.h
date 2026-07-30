#ifndef CGF_PP_H
#define CGF_PP_H

#include <stdbool.h>
#include <stddef.h>

#include "diag.h"
#include "util/arena.h"
#include "util/base.h"
#include "util/buf.h"
#include "util/intern.h"

/* ISO C17 translation phases 1-3: ingestion (normalization), line splicing,
 * pp-tokenization. Directives are Sprint 4, macro expansion Sprint 5,
 * _Pragma Sprint 6. */

typedef u16 FileId; /* 1-based; 0 invalid */

typedef struct SourceFile {
    FileId id;
    u32 diag_file_id;  /* the Sprint-0 diag module's id for this buffer */
    const char *path;  /* as spelled on the command line / #include */
    char *contents;    /* normalized (CRLF/CR -> LF), NUL-terminated */
    u32 size;          /* bytes, excluding the NUL */
    u32 *line_offsets; /* byte offset of each physical line start */
    u32 nlines;
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
} LocEnt;

typedef struct LocTable {
    LocEnt *ents;
    size_t len;
    size_t cap;
} LocTable;

void pp_loc_init(LocTable *t);
void pp_loc_free(LocTable *t);
SrcLoc pp_loc_file(LocTable *t, FileId f, u32 line, u32 col);
SrcLoc pp_loc_expansion(LocTable *t, SrcLoc spelled_at, SrcLoc expanded_from);
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
    PPTOK_OTHER, /* stray byte (@, `, ...): error only if it survives to
                    phase 7 — -E passes it through (gcc parity) */
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

struct HideSet; /* Sprint 5 (Prosser hide-sets); the field exists NOW */

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

/* --- Preprocessor state ------------------------------------------------ */

typedef struct Preprocessor {
    Arena *arena;
    DiagCtx *diag;
    Interner *interner;
    LocTable loc;
    SourceFile **files; /* index = FileId - 1 */
    size_t nfiles;
    size_t files_cap;
    bool trigraphs; /* -trigraphs (gcc parity: default off) */
} Preprocessor;

void pp_init(Preprocessor *pp, Arena *arena, DiagCtx *diag, Interner *interner);
/* Registers an in-memory buffer (takes a copy; used by tests and stdin). */
SourceFile *pp_source_add_buffer(Preprocessor *pp, const char *path,
                                 const char *bytes, size_t len);
/* Loads + normalizes a file; NULL + diagnostic on I/O error (exit 3). */
SourceFile *pp_source_load(Preprocessor *pp, const char *path);

/* Diagnostic at a SrcLoc (resolves through expansion entries to the
 * spelling location; chain RENDERING is Sprint 7). */
void pp_diag_at(Preprocessor *pp, DiagLevel lvl, SrcLoc loc, u32 len,
                const char *fmt, ...);

/* --- pp-lexer (phases 2+3 fused: splices are skipped, never rewritten,
 * so every token keeps its true physical location) -------------------- */

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

void pp_lexer_init(PpLexer *lx, Preprocessor *pp, SourceFile *sf);
/* False at EOF (out is then the PPTOK_EOF token). */
bool pp_lex_token(PpLexer *lx, PpToken *out);
/* ONLY valid in #include context (Sprint 4 calls it); header-names do not
 * exist elsewhere — never called from the main loop. */
bool pp_lex_header_name(PpLexer *lx, PpToken *out);

const char *pp_tok_kind_name(PpTokKind k);

#endif
