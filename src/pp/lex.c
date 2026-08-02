#include <string.h>

#include "pp/pp.h"
#include "util/buf.h"

/* Phases 2+3 fused: the lexer treats backslash-newline as an invisible
 * sequence (never rewriting the buffer), so every token keeps its true
 * physical (line,col). Token SPELLINGS have splices removed (accumulated
 * into a scratch buffer, then interned). */

void pp_lexer_init(PpLexer *lx, Preprocessor *pp, SourceFile *sf)
{
    memset(lx, 0, sizeof(*lx));
    lx->pp = pp;
    lx->sf = sf;
    lx->at_bol = true;
    buf_init(&lx->scratch);
}

static void linecol(const SourceFile *sf, size_t off, u32 *line, u32 *col)
{
    u32 lo = 0, hi = sf->nlines; /* find last line_offset <= off */

    while (hi - lo > 1) {
        u32 mid = lo + (hi - lo) / 2;
        if (sf->line_offsets[mid] <= off)
            lo = mid;
        else
            hi = mid;
    }
    *line = lo + 1;
    *col = (u32)(off - sf->line_offsets[lo]) + 1;
}

static SrcLoc loc_at(PpLexer *lx, size_t off)
{
    u32 line, col;

    linecol(lx->sf, off, &line, &col);
    return pp_loc_file(&lx->pp->loc, lx->sf->id, line, col);
}

/* Skips splice sequences at physical position p: backslash-newline, and the
 * gcc-matched extension backslash-whitespace-newline (warned, spliced
 * anyway). Also warns on a splice into EOF. Warnings are watermarked so
 * peeks re-crossing the same splice stay silent. */
static size_t skip_spl(PpLexer *lx, size_t p)
{
    const char *s = lx->sf->contents;

    while (s[p] == '\\') {
        size_t q = p + 1;
        bool spaced = false;

        while (s[q] == ' ' || s[q] == '\t' || s[q] == '\v' || s[q] == '\f') {
            q++;
            spaced = true;
        }
        if (s[q] != '\n')
            break;
        /* A backslash before the newline WE synthesized (phase 1, for a
         * file not ending in one) is not a splice: gcc keeps it. */
        if (lx->sf->synth_final_newline && q + 1 >= lx->sf->size)
            break;
        if (spaced && p >= lx->warned_upto) {
            pp_warn_at(lx->pp, WARN_BACKSLASH_NEWLINE_ESCAPE, loc_at(lx, p), 1,
                       "backslash and newline separated by space");
            lx->warned_upto = p + 1;
        }
        if (q + 1 >= lx->sf->size && p >= lx->warned_upto) {
            pp_warn_at(lx->pp, WARN_NEWLINE_EOF, loc_at(lx, p), 1,
                       "backslash-newline at end of file");
            lx->warned_upto = p + 1;
        }
        p = q + 1;
    }
    return p;
}

/* Logical character access. peek does not move; take consumes the current
 * logical char and appends it to the scratch spelling. */
static char peekc(PpLexer *lx)
{
    return lx->sf->contents[skip_spl(lx, lx->pos)];
}

static char peekc2(PpLexer *lx)
{
    size_t p = skip_spl(lx, lx->pos);

    if (lx->sf->contents[p] == '\0')
        return '\0';
    p = skip_spl(lx, p + 1);
    return lx->sf->contents[p];
}

static char peekc3(PpLexer *lx)
{
    size_t p = skip_spl(lx, lx->pos);

    if (lx->sf->contents[p] == '\0')
        return '\0';
    p = skip_spl(lx, p + 1);
    if (lx->sf->contents[p] == '\0')
        return '\0';
    p = skip_spl(lx, p + 1);
    return lx->sf->contents[p];
}

static char take(PpLexer *lx)
{
    char c;

    lx->pos = skip_spl(lx, lx->pos);
    c = lx->sf->contents[lx->pos];
    if (c != '\0') {
        buf_push_u8(&lx->scratch, (u8)c);
        lx->pos++;
    }
    return c;
}

static void drop(PpLexer *lx) /* consume without spelling (whitespace) */
{
    lx->pos = skip_spl(lx, lx->pos);
    if (lx->sf->contents[lx->pos] != '\0')
        lx->pos++;
}

static void comment_take(PpLexer *lx, Buf *body)
{
    char c;

    lx->pos = skip_spl(lx, lx->pos);
    c = lx->sf->contents[lx->pos];
    if (c != '\0') {
        buf_push_u8(body, (u8)c);
        lx->pos++;
    }
}

static void record_comment(PpLexer *lx, size_t start, size_t end,
                           const Buf *body)
{
    Preprocessor *pp = lx->pp;
    PpComment *c;
    u32 id;

    /* A physical file may be lexed more than once after #undef'ing its
     * include guard. Its comment metadata remains one immutable record. */
    for (size_t i = pp->ncomments; i > 0; i--) {
        c = &pp->comments[i - 1];
        if (c->file == lx->sf->id && c->start_offset == (u32)start)
            return;
    }
    if (pp->ncomments == pp->comments_cap) {
        size_t cap = pp->comments_cap ? pp->comments_cap * 2 : 16;
        PpComment *nv =
            arena_alloc(pp->arena, cap * sizeof(*nv), _Alignof(PpComment));

        if (pp->ncomments)
            memcpy(nv, pp->comments, pp->ncomments * sizeof(*nv));
        pp->comments = nv;
        pp->comments_cap = cap;
    }
    id = intern(pp->interner, (const char *)body->data, body->len);
    c = &pp->comments[pp->ncomments++];
    c->body = intern_str(pp->interner, id);
    c->loc = loc_at(lx, start);
    c->body_len = (u32)body->len;
    c->start_offset = (u32)start;
    c->end_offset = (u32)end;
    c->before_offset = UINT32_MAX;
    c->file = lx->sf->id;
}

static bool is_ident_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '$' /* gcc extension */ || (unsigned char)c >= 0x80;
}

static bool is_ident_cont(char c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Consumes a UCN if one starts at the cursor (backslash + u/U). Malformed
 * UCNs are diagnosed and consumed as far as they go. */
static bool take_ucn(PpLexer *lx)
{
    char u = peekc2(lx);
    int need, got = 0;

    if (peekc(lx) != '\\' || (u != 'u' && u != 'U'))
        return false;
    need = (u == 'u') ? 4 : 8;
    take(lx); /* backslash */
    take(lx); /* u / U */
    while (got < need && is_hex(peekc(lx))) {
        take(lx);
        got++;
    }
    if (got < need) {
        pp_diag_at(lx->pp, DIAG_ERROR, loc_at(lx, lx->pos), 1,
                   "incomplete universal character name");
        lx->had_error = true;
    }
    return true;
}

/* Whitespace and comments; maintains BOL/SPACE state. Comments count as one
 * space. Splices happen first, so a // comment ending in backslash swallows
 * the next physical line — that is ISO semantics and bites real code. */
static void skip_ws_and_comments(PpLexer *lx, bool record)
{
    for (;;) {
        char c = peekc(lx);

        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            drop(lx);
            lx->pending_space = true;
        } else if (c == '\n') {
            drop(lx);
            lx->at_bol = true;
            lx->pending_space = false;
        } else if (c == '/' && peekc2(lx) == '*') {
            size_t start = skip_spl(lx, lx->pos);
            Buf body;

            buf_init(&body);
            drop(lx);
            drop(lx);
            for (;;) {
                char d = peekc(lx);
                if (d == '\0') {
                    if (record)
                        record_comment(lx, start, lx->pos, &body);
                    buf_free(&body);
                    pp_diag_at(lx->pp, DIAG_ERROR, loc_at(lx, start), 2,
                               "unterminated /* comment");
                    lx->had_error = true;
                    return;
                }
                if (d == '*' && peekc2(lx) == '/') {
                    drop(lx);
                    drop(lx);
                    break;
                }
                comment_take(lx, &body);
            }
            if (record)
                record_comment(lx, start, lx->pos, &body);
            buf_free(&body);
            lx->pending_space = true;
        } else if (c == '/' && peekc2(lx) == '/') {
            size_t start = skip_spl(lx, lx->pos);
            Buf body;

            buf_init(&body);
            drop(lx);
            drop(lx);
            while (peekc(lx) != '\n' && peekc(lx) != '\0')
                comment_take(lx, &body);
            if (record)
                record_comment(lx, start, lx->pos, &body);
            buf_free(&body);
            lx->pending_space = true;
        } else {
            return;
        }
    }
}

/* Character/string literal body, opening quote already taken. Backslash
 * escapes the next logical char (any, including the quote); an unescaped
 * newline is an error and is NOT consumed (gcc parity: never scan past the
 * logical end of line). */
static void take_literal_tail(PpLexer *lx, char quote, size_t start)
{
    for (;;) {
        char c = peekc(lx);

        if (c == '\0' || c == '\n') {
            pp_diag_at(lx->pp, DIAG_ERROR, loc_at(lx, start), 1,
                       "missing terminating %c character",
                       quote == '"' ? '"' : '\'');
            lx->had_error = true;
            return;
        }
        if (c == '\\') {
            take(lx);
            if (peekc(lx) != '\n' && peekc(lx) != '\0')
                take(lx);
            continue;
        }
        take(lx);
        if (c == quote)
            return;
    }
}

/* pp-number, first char (digit, or . before digit) already taken. The
 * grammar is deliberately greedy: digits, identifier chars (incl. UCNs),
 * dots, and [eEpP] followed by a sign all extend it — 0x1e+2 is ONE
 * pp-number. Conversion errors are Sprint 8's, not ours. */
static void take_ppnum_tail(PpLexer *lx)
{
    for (;;) {
        char c = peekc(lx);

        if ((c == 'e' || c == 'E' || c == 'p' || c == 'P') &&
            (peekc2(lx) == '+' || peekc2(lx) == '-')) {
            take(lx);
            take(lx);
        } else if (is_digit(c) || c == '.' || is_ident_start(c)) {
            take(lx);
        } else if (c == '\\' && (peekc2(lx) == 'u' || peekc2(lx) == 'U')) {
            take_ucn(lx);
        } else {
            return;
        }
    }
}

/* Max-munch punctuator, first char at cursor. Digraphs are ISO and always
 * on; they map to the primary punct value but keep their own spelling. */
static bool take_punct(PpLexer *lx, u16 *out)
{
    char a = peekc(lx), b, c;

    switch (a) {
    case '[':
        take(lx);
        *out = PUNCT_LBRACKET;
        return true;
    case ']':
        take(lx);
        *out = PUNCT_RBRACKET;
        return true;
    case '(':
        take(lx);
        *out = PUNCT_LPAREN;
        return true;
    case ')':
        take(lx);
        *out = PUNCT_RPAREN;
        return true;
    case '{':
        take(lx);
        *out = PUNCT_LBRACE;
        return true;
    case '}':
        take(lx);
        *out = PUNCT_RBRACE;
        return true;
    case '?':
        take(lx);
        *out = PUNCT_QUESTION;
        return true;
    case ';':
        take(lx);
        *out = PUNCT_SEMI;
        return true;
    case ',':
        take(lx);
        *out = PUNCT_COMMA;
        return true;
    case '~':
        take(lx);
        *out = PUNCT_TILDE;
        return true;
    default:
        break;
    }

    b = peekc2(lx);
    c = peekc3(lx);
    switch (a) {
    case '.':
        if (b == '.' && c == '.') {
            take(lx);
            take(lx);
            take(lx);
            *out = PUNCT_ELLIPSIS;
            return true;
        }
        take(lx);
        *out = PUNCT_DOT;
        return true;
    case '-':
        take(lx);
        if (peekc(lx) == '>') {
            take(lx);
            *out = PUNCT_ARROW;
            return true;
        }
        if (peekc(lx) == '-') {
            take(lx);
            *out = PUNCT_MINUSMINUS;
            return true;
        }
        if (peekc(lx) == '=') {
            take(lx);
            *out = PUNCT_MINUS_ASSIGN;
            return true;
        }
        *out = PUNCT_MINUS;
        return true;
    case '+':
        take(lx);
        if (peekc(lx) == '+') {
            take(lx);
            *out = PUNCT_PLUSPLUS;
            return true;
        }
        if (peekc(lx) == '=') {
            take(lx);
            *out = PUNCT_PLUS_ASSIGN;
            return true;
        }
        *out = PUNCT_PLUS;
        return true;
    case '&':
        take(lx);
        if (peekc(lx) == '&') {
            take(lx);
            *out = PUNCT_AMPAMP;
            return true;
        }
        if (peekc(lx) == '=') {
            take(lx);
            *out = PUNCT_AMP_ASSIGN;
            return true;
        }
        *out = PUNCT_AMP;
        return true;
    case '|':
        take(lx);
        if (peekc(lx) == '|') {
            take(lx);
            *out = PUNCT_PIPEPIPE;
            return true;
        }
        if (peekc(lx) == '=') {
            take(lx);
            *out = PUNCT_PIPE_ASSIGN;
            return true;
        }
        *out = PUNCT_PIPE;
        return true;
    case '*':
        take(lx);
        if (peekc(lx) == '=') {
            take(lx);
            *out = PUNCT_STAR_ASSIGN;
            return true;
        }
        *out = PUNCT_STAR;
        return true;
    case '/':
        take(lx);
        if (peekc(lx) == '=') {
            take(lx);
            *out = PUNCT_SLASH_ASSIGN;
            return true;
        }
        *out = PUNCT_SLASH;
        return true;
    case '!':
        take(lx);
        if (peekc(lx) == '=') {
            take(lx);
            *out = PUNCT_NOTEQ;
            return true;
        }
        *out = PUNCT_BANG;
        return true;
    case '=':
        take(lx);
        if (peekc(lx) == '=') {
            take(lx);
            *out = PUNCT_EQEQ;
            return true;
        }
        *out = PUNCT_ASSIGN;
        return true;
    case '^':
        take(lx);
        if (peekc(lx) == '=') {
            take(lx);
            *out = PUNCT_CARET_ASSIGN;
            return true;
        }
        *out = PUNCT_CARET;
        return true;
    case '#':
        take(lx);
        if (peekc(lx) == '#') {
            take(lx);
            *out = PUNCT_HASHHASH;
            return true;
        }
        *out = PUNCT_HASH;
        return true;
    case '<':
        if (b == '<') {
            take(lx);
            take(lx);
            if (peekc(lx) == '=') {
                take(lx);
                *out = PUNCT_SHL_ASSIGN;
                return true;
            }
            *out = PUNCT_SHL;
            return true;
        }
        if (b == '=') {
            take(lx);
            take(lx);
            *out = PUNCT_LE;
            return true;
        }
        if (b == ':') {
            take(lx);
            take(lx);
            *out = PUNCT_LBRACKET;
            return true;
        }
        if (b == '%') {
            take(lx);
            take(lx);
            *out = PUNCT_LBRACE;
            return true;
        }
        take(lx);
        *out = PUNCT_LT;
        return true;
    case '>':
        if (b == '>') {
            take(lx);
            take(lx);
            if (peekc(lx) == '=') {
                take(lx);
                *out = PUNCT_SHR_ASSIGN;
                return true;
            }
            *out = PUNCT_SHR;
            return true;
        }
        if (b == '=') {
            take(lx);
            take(lx);
            *out = PUNCT_GE;
            return true;
        }
        take(lx);
        *out = PUNCT_GT;
        return true;
    case '%':
        if (b == ':') {
            take(lx);
            take(lx);
            /* %:%: is the ## digraph — 4-char max munch. */
            if (peekc(lx) == '%' && peekc2(lx) == ':') {
                take(lx);
                take(lx);
                *out = PUNCT_HASHHASH;
                return true;
            }
            *out = PUNCT_HASH;
            return true;
        }
        if (b == '=') {
            take(lx);
            take(lx);
            *out = PUNCT_PERCENT_ASSIGN;
            return true;
        }
        if (b == '>') {
            take(lx);
            take(lx);
            *out = PUNCT_RBRACE;
            return true;
        }
        take(lx);
        *out = PUNCT_PERCENT;
        return true;
    case ':':
        take(lx);
        if (peekc(lx) == '>') {
            take(lx);
            *out = PUNCT_RBRACKET;
            return true;
        }
        *out = PUNCT_COLON;
        return true;
    default:
        return false;
    }
}

static void finish_token(PpLexer *lx, PpToken *out, PpTokKind kind, u16 punct,
                         SrcLoc loc)
{
    u32 id = intern(lx->pp->interner, (const char *)lx->scratch.data,
                    lx->scratch.len);

    out->spelling = intern_str(lx->pp->interner, id);
    out->hideset = NULL;
    out->loc = loc;
    out->len = (u32)lx->scratch.len;
    out->kind = (u8)kind;
    out->flags = (u8)((lx->at_bol ? PPTOK_F_BOL : 0) |
                      (lx->pending_space ? PPTOK_F_SPACE : 0));
    out->punct = punct;
    lx->at_bol = false;
    lx->pending_space = false;
}

/* Is this identifier spelling an encoding prefix, with a quote adjacent? */
static char prefix_quote(PpLexer *lx)
{
    const Buf *b = &lx->scratch;
    char q = peekc(lx);

    if (q != '"' && q != '\'')
        return '\0';
    if (b->len == 1 &&
        (b->data[0] == 'L' || b->data[0] == 'u' || b->data[0] == 'U'))
        return q;
    if (b->len == 2 && b->data[0] == 'u' && b->data[1] == '8' && q == '"')
        return q;
    return '\0';
}

bool pp_lex_token(PpLexer *lx, PpToken *out)
{
    size_t start;
    size_t comment_base = lx->pp->ncomments;
    SrcLoc loc;
    char c;

    skip_ws_and_comments(lx, true);
    lx->scratch.len = 0;

    start = skip_spl(lx, lx->pos);
    for (size_t i = comment_base; i < lx->pp->ncomments; i++)
        lx->pp->comments[i].before_offset = (u32)start;
    lx->pos = start;
    loc = loc_at(lx, start);
    c = peekc(lx);

    if (c == '\0') {
        finish_token(lx, out, PPTOK_EOF, 0, loc);
        return false;
    }

    if (is_ident_start(c)) {
        take(lx);
        for (;;) {
            char d = peekc(lx);
            if (is_ident_cont(d)) {
                take(lx);
            } else if (d == '\\' && (peekc2(lx) == 'u' || peekc2(lx) == 'U')) {
                take_ucn(lx);
            } else {
                break;
            }
        }
        {
            char q = prefix_quote(lx);
            if (q) {
                take(lx); /* the quote */
                take_literal_tail(lx, q, start);
                finish_token(lx, out, q == '"' ? PPTOK_STRLIT : PPTOK_CHARCONST,
                             0, loc);
                return true;
            }
        }
        finish_token(lx, out, PPTOK_IDENT, 0, loc);
        return true;
    }

    if (c == '\\' && (peekc2(lx) == 'u' || peekc2(lx) == 'U')) {
        /* UCN-initial identifier. */
        take_ucn(lx);
        while (
            is_ident_cont(peekc(lx)) ||
            (peekc(lx) == '\\' && (peekc2(lx) == 'u' || peekc2(lx) == 'U'))) {
            if (peekc(lx) == '\\')
                take_ucn(lx);
            else
                take(lx);
        }
        finish_token(lx, out, PPTOK_IDENT, 0, loc);
        return true;
    }

    if (is_digit(c) || (c == '.' && is_digit(peekc2(lx)))) {
        take(lx);
        take_ppnum_tail(lx);
        finish_token(lx, out, PPTOK_PPNUM, 0, loc);
        return true;
    }

    if (c == '"' || c == '\'') {
        take(lx);
        take_literal_tail(lx, c, start);
        finish_token(lx, out, c == '"' ? PPTOK_STRLIT : PPTOK_CHARCONST, 0,
                     loc);
        return true;
    }

    {
        u16 punct;
        if (take_punct(lx, &punct)) {
            finish_token(lx, out, PPTOK_PUNCT, punct, loc);
            return true;
        }
    }

    /* Stray byte: a token that can never begin a pp-token. -E passes it
     * through; it is an error only if it survives to phase 7. */
    take(lx);
    finish_token(lx, out, PPTOK_OTHER, 0, loc);
    return true;
}

/* True iff the next token (after whitespace/comments) begins a new logical
 * line, or EOF — WITHOUT consuming anything. The directive engine uses this
 * to find end-of-directive so that a directive's effects (#line remaps,
 * #define entries) are recorded BEFORE the next line's tokens are lexed —
 * their diagnostics must already see those effects. The splice-warning
 * watermark deliberately stays advanced (no duplicate warnings on the
 * re-scan). */
bool pp_lex_at_line_end(PpLexer *lx)
{
    size_t save_pos = lx->pos;
    bool save_bol = lx->at_bol;
    bool save_space = lx->pending_space;
    bool r;

    skip_ws_and_comments(lx, false);
    r = lx->at_bol || peekc(lx) == '\0';
    lx->pos = save_pos;
    lx->at_bol = save_bol;
    lx->pending_space = save_space;
    return r;
}

bool pp_lex_header_name(PpLexer *lx, PpToken *out)
{
    size_t start;
    size_t comment_base = lx->pp->ncomments;
    SrcLoc loc;
    char c, close;

    skip_ws_and_comments(lx, true);
    lx->scratch.len = 0;
    start = skip_spl(lx, lx->pos);
    for (size_t i = comment_base; i < lx->pp->ncomments; i++)
        lx->pp->comments[i].before_offset = (u32)start;
    lx->pos = start;
    loc = loc_at(lx, start);
    c = peekc(lx);

    if (c != '<' && c != '"')
        return false;
    close = (c == '<') ? '>' : '"';
    take(lx);
    for (;;) {
        char d = peekc(lx);
        if (d == '\0' || d == '\n') {
            pp_diag_at(lx->pp, DIAG_ERROR, loc_at(lx, start), 1,
                       "missing terminating %c in header name", close);
            lx->had_error = true;
            break;
        }
        take(lx);
        if (d == close)
            break;
    }
    finish_token(lx, out, PPTOK_HEADER_NAME, 0, loc);
    return true;
}

const PpComment *pp_comment_before_n(const Preprocessor *pp, Span target,
                                     size_t index)
{
    const SourceFile *sf = NULL;
    u32 off;

    if (!target.file_id || !target.line || !target.col)
        return NULL;
    for (size_t i = 0; i < pp->nfiles; i++) {
        if (pp->files[i]->diag_file_id == target.file_id) {
            sf = pp->files[i];
            break;
        }
    }
    if (!sf || target.line > sf->nlines)
        return NULL;
    off = sf->line_offsets[target.line - 1] + target.col - 1;
    if (off > sf->size)
        return NULL;
    for (size_t i = pp->ncomments; i > 0; i--) {
        const PpComment *c = &pp->comments[i - 1];

        if (c->file == sf->id && c->before_offset == off) {
            if (index == 0)
                return c;
            index--;
        }
    }
    return NULL;
}

const PpComment *pp_comment_before(const Preprocessor *pp, Span target)
{
    return pp_comment_before_n(pp, target, 0);
}

const char *pp_tok_kind_name(PpTokKind k)
{
    switch (k) {
    case PPTOK_IDENT:
        return "ident";
    case PPTOK_PPNUM:
        return "ppnum";
    case PPTOK_CHARCONST:
        return "char";
    case PPTOK_STRLIT:
        return "str";
    case PPTOK_PUNCT:
        return "punct";
    case PPTOK_HEADER_NAME:
        return "header";
    case PPTOK_OTHER:
        return "other";
    case PPTOK_PLACEMARKER:
        return "placemarker"; /* must never escape macro.c */
    case PPTOK_EOF:
        return "eof";
    }
    CGF_ICE("pp_tok_kind_name: bad kind %d", (int)k);
}
