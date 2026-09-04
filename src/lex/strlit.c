#include <string.h>

#include "lex/lex.h"

/* Character and string literals: escape/UCN decoding into the TARGET
 * execution charset (UTF-8 for every v0.1.0 target), plus translation
 * phase 6 concatenation. Sema never re-decodes — the payload is final. */

typedef struct {
    Preprocessor *pp;
    SrcLoc loc;
    u32 len;
    const LangOpts *lang;
    IntWidths w;
    bool failed;
} LitCtx;

static void lit_err(LitCtx *c, const char *fmt, ...)
{
    va_list ap;
    char msg[256];

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    c->failed = true;
    pp_diag_at(c->pp, DIAG_ERROR, c->loc, c->len, "%s", msg);
}

/* Reads the encoding prefix; *i lands on the opening quote. */
static EncPrefix read_prefix(const char *s, u32 len, u32 *i)
{
    if (*i < len && s[*i] == 'L') {
        (*i)++;
        return ENC_WIDE;
    }
    if (*i < len && s[*i] == 'U') {
        (*i)++;
        return ENC_U32;
    }
    if (*i < len && s[*i] == 'u') {
        if (*i + 1 < len && s[*i + 1] == '8') {
            *i += 2;
            return ENC_U8;
        }
        (*i)++;
        return ENC_U16;
    }
    return ENC_NONE;
}

/* UCN constraints (6.4.3p2): a UCN may not name a character below U+00A0
 * (except $, @, `) nor a surrogate (U+D800..DFFF) — in identifiers,
 * character constants, and strings alike. */
static bool ucn_value_ok(u32 v)
{
    if (v < 0xA0)
        return v == '$' || v == '@' || v == '`';
    if (v >= 0xD800 && v <= 0xDFFF)
        return false;
    return v <= 0x10FFFF;
}

/* Phase 1 has already preserved source UTF-8 bytes. Decode exactly one scalar
 * when a wide destination needs code values rather than execution-charset
 * bytes. Reject non-shortest forms, isolated continuations, surrogates, and
 * values beyond Unicode's range instead of silently manufacturing elements. */
static u32 decode_raw_utf8(LitCtx *c, const char *s, u32 len, u32 *i)
{
    u8 lead = (u8)s[*i];
    u32 ncont, min, v, k;

    if (lead < 0x80) {
        (*i)++;
        return lead;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        ncont = 1;
        min = 0x80;
        v = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        ncont = 2;
        min = 0x800;
        v = lead & 0x0F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        ncont = 3;
        min = 0x10000;
        v = lead & 0x07;
    } else {
        lit_err(c, "invalid UTF-8 sequence in literal");
        return 0;
    }

    for (k = 1; k <= ncont; k++) {
        u8 next;

        if (*i + k >= len) {
            lit_err(c, "incomplete UTF-8 sequence in literal");
            return 0;
        }
        next = (u8)s[*i + k];
        if ((next & 0xC0) != 0x80) {
            lit_err(c, "invalid UTF-8 continuation byte in literal");
            return 0;
        }
        v = (v << 6) | (next & 0x3F);
    }
    if (v < min || (v >= 0xD800 && v <= 0xDFFF) || v > 0x10FFFF) {
        lit_err(c, "invalid Unicode scalar value in UTF-8 literal");
        return 0;
    }
    *i += ncont + 1;
    return v;
}

/* Decodes ONE escape/UCN/plain char starting at s[*i] (past the opening
 * quote). Raw UTF-8 is decoded only for a wide destination; escapes and UCNs
 * are already code values and must not be reinterpreted. */
static u32 decode_char(LitCtx *c, const char *s, u32 len, u32 *i,
                       bool decode_utf8, bool *is_ucn)
{
    u32 v = 0;

    *is_ucn = false;
    if (s[*i] != '\\') {
        if (decode_utf8)
            return decode_raw_utf8(c, s, len, i);
        /* Ordinary and u8 payloads preserve the UTF-8 execution bytes. */
        return (u32)(u8)s[(*i)++];
    }
    (*i)++; /* backslash */
    if (*i >= len) {
        lit_err(c, "incomplete escape sequence");
        return 0;
    }
    switch (s[*i]) {
    case 'n':
        (*i)++;
        return '\n';
    case 't':
        (*i)++;
        return '\t';
    case 'r':
        (*i)++;
        return '\r';
    case 'a':
        (*i)++;
        return '\a';
    case 'b':
        (*i)++;
        return '\b';
    case 'f':
        (*i)++;
        return '\f';
    case 'v':
        (*i)++;
        return '\v';
    case '\\':
        (*i)++;
        return '\\';
    case '\'':
        (*i)++;
        return '\'';
    case '"':
        (*i)++;
        return '"';
    case '?':
        (*i)++;
        return '?';
    case 'e':
        /* \e (ESC) is a GNU extension; accepted everywhere, pedwarned in
         * strict std modes (Sprint 37 makes the flag real). */
        (*i)++;
        if (!c->lang->gnu_mode && c->lang->pedantic)
            pp_warn_at(c->pp, WARN_PEDANTIC, c->loc, c->len,
                       "'\\e' is a GNU extension");
        return 27;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7': {
        /* Octal: at most THREE digits, so '\1234' is '\123' then '4'
         * (a multi-char constant), not an out-of-range escape. */
        int k = 0;
        while (k < 3 && *i < len && s[*i] >= '0' && s[*i] <= '7') {
            v = v * 8 + (u32)(s[*i] - '0');
            (*i)++;
            k++;
        }
        return v;
    }
    case 'x': {
        /* Hex: UNBOUNDED digits, then range-checked by the caller against
         * the constant's element width. */
        int k = 0;
        (*i)++;
        while (*i < len) {
            char d = s[*i];
            u32 dv;
            if (d >= '0' && d <= '9')
                dv = (u32)(d - '0');
            else if (d >= 'a' && d <= 'f')
                dv = (u32)(d - 'a' + 10);
            else if (d >= 'A' && d <= 'F')
                dv = (u32)(d - 'A' + 10);
            else
                break;
            if (v > 0x0FFFFFFFu)
                v = 0x10000000u; /* saturate; range check reports it */
            else
                v = v * 16 + dv;
            (*i)++;
            k++;
        }
        if (k == 0)
            lit_err(c, "\\x used with no following hex digits");
        return v;
    }
    case 'u':
    case 'U': {
        u32 want = s[*i] == 'u' ? 4 : 8, k = 0;
        (*i)++;
        while (k < want && *i < len) {
            char d = s[*i];
            u32 dv;
            if (d >= '0' && d <= '9')
                dv = (u32)(d - '0');
            else if (d >= 'a' && d <= 'f')
                dv = (u32)(d - 'a' + 10);
            else if (d >= 'A' && d <= 'F')
                dv = (u32)(d - 'A' + 10);
            else
                break;
            v = v * 16 + dv;
            (*i)++;
            k++;
        }
        if (k != want) {
            lit_err(c, "incomplete universal character name");
            return 0;
        }
        if (!ucn_value_ok(v)) {
            lit_err(c, "\\%c%0*X is not a valid universal character",
                    want == 4 ? 'u' : 'U', (int)want, (unsigned)v);
            return 0;
        }
        *is_ucn = true;
        return v;
    }
    default:
        lit_err(c, "unknown escape sequence '\\%c'", s[*i]);
        (*i)++;
        return 0;
    }
}

/* Appends one code point as UTF-8 (the execution charset). */
static void put_utf8(Buf *b, u32 v)
{
    if (v < 0x80) {
        buf_push_u8(b, (u8)v);
    } else if (v < 0x800) {
        buf_push_u8(b, (u8)(0xC0 | (v >> 6)));
        buf_push_u8(b, (u8)(0x80 | (v & 0x3F)));
    } else if (v < 0x10000) {
        buf_push_u8(b, (u8)(0xE0 | (v >> 12)));
        buf_push_u8(b, (u8)(0x80 | ((v >> 6) & 0x3F)));
        buf_push_u8(b, (u8)(0x80 | (v & 0x3F)));
    } else {
        buf_push_u8(b, (u8)(0xF0 | (v >> 18)));
        buf_push_u8(b, (u8)(0x80 | ((v >> 12) & 0x3F)));
        buf_push_u8(b, (u8)(0x80 | ((v >> 6) & 0x3F)));
        buf_push_u8(b, (u8)(0x80 | (v & 0x3F)));
    }
}

static void put_u16(Buf *b, u32 v)
{
    /* Little-endian UTF-16; non-BMP becomes a surrogate pair. */
    if (v >= 0x10000) {
        u32 x = v - 0x10000;
        u32 hi = 0xD800 + (x >> 10), lo = 0xDC00 + (x & 0x3FF);
        buf_push_u8(b, (u8)(hi & 0xFF));
        buf_push_u8(b, (u8)(hi >> 8));
        buf_push_u8(b, (u8)(lo & 0xFF));
        buf_push_u8(b, (u8)(lo >> 8));
        return;
    }
    buf_push_u8(b, (u8)(v & 0xFF));
    buf_push_u8(b, (u8)(v >> 8));
}

static void put_u32le(Buf *b, u32 v)
{
    buf_push_u8(b, (u8)(v & 0xFF));
    buf_push_u8(b, (u8)((v >> 8) & 0xFF));
    buf_push_u8(b, (u8)((v >> 16) & 0xFF));
    buf_push_u8(b, (u8)((v >> 24) & 0xFF));
}

void lex_char_const(Preprocessor *pp, Token *t, const char *sp, u32 len,
                    const LangOpts *lang, IntWidths w, SrcLoc loc)
{
    LitCtx c;
    u32 i = 0;
    EncPrefix enc;
    u64 v = 0;
    int nchars = 0;
    u32 elem_bits;
    bool elem_signed;

    memset(&c, 0, sizeof(c));
    c.pp = pp;
    c.loc = loc;
    c.len = len;
    c.lang = lang;
    c.w = w;

    enc = read_prefix(sp, len, &i);
    /* NOTE: u8'' cannot actually reach here in c17 — the preprocessor
     * only fuses u8 with a double quote, so u8'a' is identifier + char
     * constant (verified against gcc -std=c17). The check stands for the
     * day C23 support lands and the pp starts fusing it. */
    if (enc == ENC_U8 && !std_is_c11_or_later(lang->std)) {
        lit_err(&c, "u8 character constants are a C23 feature");
        return;
    }
    if ((enc == ENC_U16 || enc == ENC_U32) && !std_is_c11_or_later(lang->std))
        lit_err(&c, "u/U character constants are a C11 feature");
    if (i >= len || sp[i] != '\'') {
        lit_err(&c, "malformed character constant");
        return;
    }
    i++;

    /* Element width: what a single char must fit in. */
    switch (enc) {
    case ENC_NONE:
        elem_bits = w.char_bits;
        elem_signed = w.char_signed;
        break;
    case ENC_WIDE:
        elem_bits = w.wchar_bits;
        elem_signed = true; /* wchar_t is `int` on our targets */
        break;
    case ENC_U16:
        elem_bits = 16;
        elem_signed = false;
        break;
    case ENC_U32:
    case ENC_U8:
    default:
        elem_bits = 32;
        elem_signed = false;
        break;
    }

    while (i < len && sp[i] != '\'') {
        bool is_ucn;
        u32 cv = decode_char(&c, sp, len, &i,
                             enc == ENC_WIDE || enc == ENC_U16 ||
                                 enc == ENC_U32,
                             &is_ucn);

        if (c.failed)
            return;
        if (elem_bits < 32 && cv >= (1u << elem_bits) && !is_ucn) {
            lit_err(&c, "escape sequence out of range for type");
            return;
        }
        nchars++;
        if (enc == ENC_NONE) {
            /* Multi-char: gcc accumulates BIG-ENDIAN, keeping the last
             * four bytes. Implementation-defined, and this is gcc's. */
            v = (v << 8) | (cv & 0xFF);
        } else {
            v = cv; /* L'ab': gcc keeps the LAST character */
        }
    }
    if (i >= len || sp[i] != '\'') {
        lit_err(&c, "missing terminating ' character");
        return;
    }
    if (nchars == 0) {
        lit_err(&c, "empty character constant");
        return;
    }
    if (nchars > 1) {
        pp_warn_at(pp, WARN_MULTICHAR, loc, len,
                   enc == ENC_NONE ? "multi-character character constant"
                                   : "character constant too long for its "
                                     "type");
        if (enc == ENC_NONE && nchars > 4)
            v &= 0xFFFFFFFFull; /* keep the last four bytes (gcc) */
    }

    /* A character constant has type INT in C — not char. (The C++ rule is
     * different and catches people out.) The VALUE is sign-extended from
     * the element width for a single ordinary char. */
    t->int_type = ITY_INT;
    t->enc = (u8)enc;
    if (enc == ENC_NONE && nchars == 1 && elem_signed && (v & 0x80))
        t->int_val = (u64)(i64)(i8)(u8)v;
    else
        t->int_val = v;
}

void lex_string_lit(Preprocessor *pp, Token *t, const PpToken *run, u32 count,
                    const LangOpts *lang, IntWidths w, Arena *arena)
{
    LitCtx c;
    Buf payload;
    EncPrefix result_enc = ENC_NONE;
    u32 k;
    u32 nelems = 0;

    memset(&c, 0, sizeof(c));
    c.pp = pp;
    c.loc = run[0].loc;
    c.len = run[0].len;
    c.lang = lang;
    c.w = w;

    /* Phase-6 prefix rules (6.4.5p5): plain + prefixed adopts the prefix;
     * two DIFFERENT prefixes is an error (the standard leaves some combos
     * impl-defined; gcc 8 rejects, so do we). */
    for (k = 0; k < count; k++) {
        u32 i = 0;
        EncPrefix e = read_prefix(run[k].spelling, run[k].len, &i);

        if (e == ENC_NONE)
            continue;
        if (result_enc != ENC_NONE && result_enc != e) {
            c.loc = run[k].loc;
            c.len = run[k].len;
            lit_err(&c, "concatenation of differently-prefixed string "
                        "literals");
            return;
        }
        result_enc = e;
    }
    if ((result_enc == ENC_U16 || result_enc == ENC_U32 ||
         result_enc == ENC_U8) &&
        !std_is_c11_or_later(lang->std)) {
        lit_err(&c, "u/U/u8 string literals are a C11 feature");
        return;
    }

    buf_init(&payload);
    for (k = 0; k < count; k++) {
        const char *sp = run[k].spelling;
        u32 len = run[k].len, i = 0;

        c.loc = run[k].loc;
        c.len = len;
        read_prefix(sp, len, &i);
        if (i >= len || sp[i] != '"') {
            lit_err(&c, "malformed string literal");
            buf_free(&payload);
            return;
        }
        i++;
        /* Each literal's escapes are decoded INDEPENDENTLY before
         * concatenation: "\x12" "3" is two chars, never \x123. */
        while (i < len && sp[i] != '"') {
            bool is_ucn;
            u32 cv = decode_char(&c, sp, len, &i,
                                 result_enc == ENC_WIDE ||
                                     result_enc == ENC_U16 ||
                                     result_enc == ENC_U32,
                                 &is_ucn);

            if (c.failed) {
                buf_free(&payload);
                return;
            }
            switch (result_enc) {
            case ENC_WIDE:
            case ENC_U32:
                put_u32le(&payload, cv);
                break;
            case ENC_U16:
                put_u16(&payload, cv);
                nelems += cv >= 0x10000 ? 1 : 0; /* surrogate pair = 2 */
                break;
            case ENC_NONE:
            case ENC_U8:
            default:
                if (is_ucn) {
                    size_t before = payload.len;
                    put_utf8(&payload, cv);
                    nelems += (u32)(payload.len - before) - 1;
                } else {
                    buf_push_u8(&payload, (u8)cv);
                }
                break;
            }
            nelems++;
        }
    }

    /* The implicit NUL is appended ONCE, after all concatenation. */
    switch (result_enc) {
    case ENC_WIDE:
    case ENC_U32:
        put_u32le(&payload, 0);
        break;
    case ENC_U16:
        put_u16(&payload, 0);
        break;
    default:
        buf_push_u8(&payload, 0);
        break;
    }

    t->str.nbytes =
        (u32)payload.len - (result_enc == ENC_WIDE || result_enc == ENC_U32 ? 4
                            : result_enc == ENC_U16 ? 2
                                                    : 1);
    t->str.nelems = nelems;
    t->str.enc = result_enc;
    {
        u8 *bytes = arena_alloc(arena, payload.len, 1);
        memcpy(bytes, payload.data, payload.len);
        t->str.bytes = bytes;
    }
    buf_free(&payload);
}
