#include "lex/lex.h"

#include <string.h>

/* --- target integer widths (never host sizeof) ------------------------- */

IntWidths cgf_target_int_widths(TargetSpec t)
{
    IntWidths w;

    w.char_bits = 8;
    w.short_bits = 16;
    w.int_bits = 32;
    w.long_bits = 64;
    w.llong_bits = 64;
    w.wchar_bits = 32;
    /* Plain `char` signedness is per-target, and this is exactly why the
     * lexer takes a TargetSpec instead of asking the host (Sprint 13's
     * conversion tables consume the same value). */
    switch (t.kind) {
    case CGF_TARGET_X86_64_LINUX_GNU:
    case CGF_TARGET_X86_64_LINUX_MUSL:
    case CGF_TARGET_X86_64_FREEBSD:
    case CGF_TARGET_ARM64_MACOS:
        w.char_signed = true;
        break;
    case CGF_TARGET_ARM64_LINUX:
        w.char_signed = false; /* the arm64-linux trap */
        break;
    }
    return w;
}

/* --- keyword table ----------------------------------------------------- */

typedef enum {
    KWG_C89,
    KWG_C99,
    KWG_C99_OR_GNU89,
    KWG_C11,
    KWG_GNU,
    KWG_ALT,
} KwGroup;

static const struct {
    const char *spelling;
    u8 group;
} kw_table[] = {
#define CGF_KEYWORD(id, spelling, group) {spelling, group},
#include "lex/keywords.def"
#undef CGF_KEYWORD
};

static const char *const kw_names[] = {
    "<none>",
#define CGF_KEYWORD(id, spelling, group) spelling,
#include "lex/keywords.def"
#undef CGF_KEYWORD
};

bool std_is_c99_or_later(CStd s)
{
    return s != STD_C89 && s != STD_GNU89;
}

bool std_is_c11_or_later(CStd s)
{
    return s == STD_C11 || s == STD_C17 || s == STD_GNU11 || s == STD_GNU17;
}

static bool group_active(KwGroup g, const LangOpts *lang)
{
    switch (g) {
    case KWG_C89:
        return true;
    case KWG_C99:
        return std_is_c99_or_later(lang->std);
    case KWG_C99_OR_GNU89:
        /* `inline` is a keyword in c99+ AND in gnu89 (with gnu89's
         * inverted inline semantics — Sprint 16 owns those). */
        return std_is_c99_or_later(lang->std) || lang->std == STD_GNU89;
    case KWG_C11:
        return std_is_c11_or_later(lang->std);
    case KWG_GNU:
        return lang->gnu_mode;
    case KWG_ALT:
        /* The __-spelled variants are keywords in EVERY mode: system
         * headers use them precisely so they work under -std=c89. */
        return true;
    }
    return false;
}

Keyword lex_keyword_lookup(const char *spelling, const LangOpts *lang)
{
    size_t i;

    /* Linear over ~70 entries, only for identifiers; the interner makes
     * the strcmp cheap (equal pointers hit first). Sprint 52 profiling found
     * scope lookup, not this table, to be the actionable quadratic hot spot. */
    for (i = 0; i < CGF_ARRAY_LEN(kw_table); i++) {
        if (strcmp(kw_table[i].spelling, spelling) == 0)
            return group_active((KwGroup)kw_table[i].group, lang)
                       ? (Keyword)(i + 1)
                       : KW_NONE;
    }
    return KW_NONE;
}

const char *lex_keyword_name(Keyword kw)
{
    if ((size_t)kw >= CGF_ARRAY_LEN(kw_names))
        CGF_ICE("lex_keyword_name: bad keyword %d", (int)kw);
    return kw_names[kw];
}

const char *lex_token_kind_name(TokenKind k)
{
    switch (k) {
    case TOK_EOF:
        return "EOF";
    case TOK_IDENT:
        return "IDENT";
    case TOK_KEYWORD:
        return "KEYWORD";
    case TOK_PUNCT:
        return "PUNCT";
    case TOK_INT_CONST:
        return "INT_CONST";
    case TOK_FLOAT_CONST:
        return "FLOAT_CONST";
    case TOK_CHAR_CONST:
        return "CHAR_CONST";
    case TOK_STRING:
        return "STRING";
    }
    CGF_ICE("lex_token_kind_name: bad kind %d", (int)k);
}

const char *lex_int_type_name(IntConstType t)
{
    switch (t) {
    case ITY_INT:
        return "int";
    case ITY_UINT:
        return "unsigned int";
    case ITY_LONG:
        return "long";
    case ITY_ULONG:
        return "unsigned long";
    case ITY_LLONG:
        return "long long";
    case ITY_ULLONG:
        return "unsigned long long";
    }
    CGF_ICE("lex_int_type_name: bad type %d", (int)t);
}

/* --- the conversion pass ----------------------------------------------- */

VEC_DECL(TokVecL, Token);

TokenList lex_convert(Preprocessor *pp, const PpToken *toks, u32 ntoks,
                      const LangOpts *lang, TargetSpec target, Arena *arena)
{
    TokVecL out = {NULL, 0, 0};
    IntWidths w = cgf_target_int_widths(target);
    TokenList list;
    u32 i = 0;

    while (i < ntoks) {
        const PpToken *p = &toks[i];
        Token t;

        memset(&t, 0, sizeof(t));
        /* The span comes from the pp-token's SrcLoc — never synthesized,
         * or Sprint 7's expansion backtraces lose their anchor. */
        t.span = pp_span(pp, p->loc, p->len);
        t.spelling = p->spelling;

        switch ((PpTokKind)p->kind) {
        case PPTOK_IDENT: {
            Keyword kw = lex_keyword_lookup(p->spelling, lang);
            t.kind = kw ? TOK_KEYWORD : TOK_IDENT;
            t.kw = (u8)kw;
            i++;
            break;
        }
        case PPTOK_PPNUM:
            if (lex_ppnum_is_float(p->spelling, p->len)) {
                t.kind = TOK_FLOAT_CONST;
                lex_float_const(pp, &t, p->spelling, p->len, lang, target,
                                p->loc);
            } else {
                t.kind = TOK_INT_CONST;
                lex_int_const(pp, &t, p->spelling, p->len, lang, w, p->loc);
            }
            i++;
            break;
        case PPTOK_CHARCONST:
            t.kind = TOK_CHAR_CONST;
            lex_char_const(pp, &t, p->spelling, p->len, lang, w, p->loc);
            i++;
            break;
        case PPTOK_STRLIT: {
            /* Translation phase 6: adjacent string literals concatenate
             * into ONE token, spanning first..last. */
            u32 count = 1;
            while (i + count < ntoks && toks[i + count].kind == PPTOK_STRLIT)
                count++;
            t.kind = TOK_STRING;
            lex_string_lit(pp, &t, &toks[i], count, lang, w, arena);
            i += count;
            break;
        }
        case PPTOK_PUNCT:
            t.kind = TOK_PUNCT;
            t.punct = p->punct;
            i++;
            break;
        case PPTOK_OTHER:
            /* A stray byte survived to phase 7: NOW it is an error
             * (until here -E passes it through, gcc parity). */
            pp_diag_at(pp, DIAG_ERROR, p->loc, p->len, "stray '%s' in program",
                       p->spelling);
            i++;
            continue;
        case PPTOK_HEADER_NAME:
        case PPTOK_PLACEMARKER:
        case PPTOK_EOF:
            CGF_ICE("lex_convert: pp-token kind %s must never reach phase 7",
                    pp_tok_kind_name((PpTokKind)p->kind));
        }
        TokVecL_push(&out, t);
    }

    {
        Token eof;

        memset(&eof, 0, sizeof(eof));
        eof.kind = TOK_EOF;
        eof.spelling = "";
        /* Inherit the LAST token's location. Without this an "unexpected
         * end of file" carries no span at all and renders as a bare
         * driver-level line with no caret — the least useful form of the
         * most confusing error. Pointing at the final token at least names
         * the construct that was left open. */
        if (out.len)
            eof.span = out.data[out.len - 1].span;
        TokVecL_push(&out, eof);
    }

    list.n = (u32)out.len;
    list.toks = arena_alloc(arena, out.len * sizeof(Token), _Alignof(Token));
    memcpy(list.toks, out.data, out.len * sizeof(Token));
    TokVecL_free(&out);
    return list;
}
