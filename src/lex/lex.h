#ifndef CGF_LEX_H
#define CGF_LEX_H

#include "diag.h"
#include "pp/pp.h"
#include "target.h"
#include "util/arena.h"
#include "util/base.h"

/* Translation phases 5-7: pp-tokens become the parser's Token stream.
 * Everything here is std- and target-parameterized; nothing reads host
 * sizeof or endianness. Spans come STRAIGHT from the pp-token — never
 * re-derive them, or Sprint 7's macro-expansion backtraces die. */

typedef enum TokenKind {
    TOK_EOF,
    TOK_IDENT,
    TOK_KEYWORD,
    TOK_PUNCT,
    TOK_INT_CONST,
    TOK_FLOAT_CONST,
    TOK_CHAR_CONST,
    TOK_STRING,
} TokenKind;

/* Encoding prefixes. The width/type mapping is in the table below and is
 * target-parameterized (wchar_t is 4 bytes on every v0.1.0 target). */
typedef enum EncPrefix {
    ENC_NONE,
    ENC_WIDE, /* L */
    ENC_U16,  /* u  (C11) */
    ENC_U32,  /* U  (C11) */
    ENC_U8,   /* u8 (C11 strings; C23 for char constants) */
} EncPrefix;

/* The C11 6.4.4.1 ladder's result. */
typedef enum IntConstType {
    ITY_INT,
    ITY_UINT,
    ITY_LONG,
    ITY_ULONG,
    ITY_LLONG,
    ITY_ULLONG,
} IntConstType;

typedef enum FloatConstType {
    FTY_FLOAT,
    FTY_DOUBLE,
    FTY_LDOUBLE,
    FTY_FLOAT128, /* the `q`/`Q` and `f128`/`F128` suffixes */
} FloatConstType;

typedef enum Keyword {
    KW_NONE = 0,
#define CGF_KEYWORD(id, spelling, group) id,
#include "lex/keywords.def"
#undef CGF_KEYWORD
    KW_COUNT
} Keyword;

typedef struct StrLit {
    const u8 *bytes; /* execution-charset payload, NUL-terminated */
    u32 nbytes;      /* excluding the terminating element */
    u32 nelems;      /* elements (chars/char16_t/char32_t), excl. NUL */
    EncPrefix enc;
} StrLit;

typedef struct Token {
    u8 kind;       /* TokenKind */
    u8 kw;         /* Keyword; KW_NONE unless kind == TOK_KEYWORD */
    u16 punct;     /* PpPunct when kind == TOK_PUNCT */
    u8 int_type;   /* IntConstType for INT/CHAR consts */
    u8 float_type; /* FloatConstType for FLOAT consts */
    u8 enc;        /* EncPrefix for char consts */
    Span span;
    const char *spelling; /* interned; idents/keywords/puncts and the exact
                             float spelling (see lex_fp_interim) */
    u64 int_val;          /* value bits, already range-classified */
    StrLit str;
} Token;

typedef struct TokenList {
    Token *toks;
    u32 n;
} TokenList;

/* Language options that reach the lexer. */
typedef struct LangOpts {
    CStd std;
    bool gnu_mode;
    bool pedantic; /* -pedantic: pedwarns become visible; parsers also use
                      this bit for dialect-sensitive syntax */
    bool fwrapv;   /* signed arithmetic wraps: suppress IR no-wrap provenance */
    bool safe_mode; /* -fsafe policy diagnostics at syntax-only boundaries */
    /* -ffreestanding: no hosted library may be assumed. gcc stops treating
     * the standard names as builtins here, so `snprintf` is just a function
     * and its format string is NOT checked without an explicit
     * `format` attribute -- measured, and it is why musl's dcngettext.c
     * diverged. A freestanding `snprintf` may be anything at all. */
    bool freestanding;
    /* -ffp-contract. gcc's DEFAULT is fast in -std=gnu* and off in ISO
     * -std=c*, which is a language policy; whether contraction actually
     * happens additionally requires optimization, because gcc contracts in
     * the optimizers. Measured, not assumed: gnu17 -O0 emits no fmadd even
     * with -ffp-contract=fast, which corrects the sprint file's claim that
     * the behavior is independent of -O. 0 = off, 1 = on, 2 = fast. */
    u8 fp_contract;
    struct WarnCtx *warnings; /* per-TU warning policy */
} LangOpts;

/* --- target type widths (the ONLY source; never host sizeof) ---------- */

typedef struct IntWidths {
    u32 char_bits;
    u32 short_bits;
    u32 int_bits;
    u32 long_bits;
    u32 llong_bits;
    u32 wchar_bits;
    bool char_signed;
} IntWidths;

IntWidths cgf_target_int_widths(TargetSpec t);

/* Std predicates shared by the lexer and (later) sema. */
bool std_is_c99_or_later(CStd s);
bool std_is_c11_or_later(CStd s);

/* --- the pass --------------------------------------------------------- */

/* Pure pp-token -> Token conversion: no I/O, no lookahead into parsing.
 * Takes the Preprocessor so spans come from pp_span and diagnostics from
 * pp_diag_at — which is how the front end inherits Sprint 7's macro
 * expansion backtraces for free. */
TokenList lex_convert(Preprocessor *pp, const PpToken *toks, u32 ntoks,
                      const LangOpts *lang, TargetSpec target, Arena *arena);

Keyword lex_keyword_lookup(const char *spelling, const LangOpts *lang);
const char *lex_keyword_name(Keyword kw);
const char *lex_token_kind_name(TokenKind k);
const char *lex_int_type_name(IntConstType t);

/* Integer/float constant analysis (numlit.c). */
void lex_int_const(Preprocessor *pp, Token *t, const char *sp, u32 len,
                   const LangOpts *lang, IntWidths w, SrcLoc loc);
void lex_float_const(Preprocessor *pp, Token *t, const char *sp, u32 len,
                     const LangOpts *lang, SrcLoc loc);
/* True if the pp-number is a floating (not integer) constant. */
bool lex_ppnum_is_float(const char *sp, u32 len);

/* The float VALUE is not computed by the lexer: the token carries the
 * exact spelling and Sprint 15's src/util/softfp.c converts it in the
 * TARGET's format. (The old host-strtod seam, debt XD-S08-FPHOST, is
 * retired.) */

/* Char/string constant analysis (strlit.c). */
void lex_char_const(Preprocessor *pp, Token *t, const char *sp, u32 len,
                    const LangOpts *lang, IntWidths w, SrcLoc loc);
/* Converts a RUN of adjacent string pp-tokens (phase 6 concatenation). */
void lex_string_lit(Preprocessor *pp, Token *t, const PpToken *run, u32 count,
                    const LangOpts *lang, IntWidths w, Arena *arena);

#endif
