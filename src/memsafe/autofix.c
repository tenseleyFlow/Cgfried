#include "memsafe/autofix.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "parse/ast.h"
#include "pp/pp.h"
#include "sema/sema.h"
#include "util/arena.h"
#include "util/buf.h"
#include "warn/warn.h"

typedef struct {
    const char *src;
    size_t len;
    size_t begin;
    size_t end;
} TextRange;

typedef struct {
    WarnCtx *warn;
    Sema *sema;
    DiagCtx *dc;
    const Preprocessor *pp;
    bool snprintf_shadowed;
} Autofix;

static AstNode *written_expr(AstNode *e)
{
    while (e && ((e->kind == AST_EXPR_CAST && e->implicit) ||
                 e->kind == AST_EXPR_PAREN))
        e = e->lhs;
    return e;
}

static bool span_offset(DiagCtx *dc, Span sp, size_t *out, const char **src_out,
                        size_t *len_out)
{
    const char *src;
    size_t len, off = 0;
    u32 line = 1;

    src = diag_file_source(dc, sp.file_id, &len);
    if (!src || !sp.line || !sp.col)
        return false;
    while (line < sp.line && off < len) {
        if (src[off++] == '\n')
            line++;
    }
    if (line != sp.line || (size_t)(sp.col - 1) > len - off)
        return false;
    off += sp.col - 1;
    if (off > len)
        return false;
    *out = off;
    if (src_out)
        *src_out = src;
    if (len_out)
        *len_out = len;
    return true;
}

static void skip_quoted(const char *src, size_t len, size_t *p, char quote)
{
    (*p)++;
    while (*p < len) {
        if (src[*p] == '\\' && *p + 1 < len) {
            *p += 2;
            continue;
        }
        if (src[(*p)++] == quote)
            return;
    }
}

static bool matching_paren(const char *src, size_t len, size_t open,
                           size_t *close)
{
    unsigned depth = 0;
    size_t p;

    if (open >= len || src[open] != '(')
        return false;
    for (p = open; p < len; p++) {
        if (src[p] == '\n')
            return false; /* only single-line edits are machine-applicable */
        if (src[p] == '\'' || src[p] == '"') {
            skip_quoted(src, len, &p, src[p]);
            p--;
            continue;
        }
        if (src[p] == '/' && p + 1 < len && src[p + 1] == '/')
            return false;
        if (src[p] == '/' && p + 1 < len && src[p + 1] == '*') {
            p += 2;
            while (p + 1 < len && !(src[p] == '*' && src[p + 1] == '/'))
                p++;
            if (p + 1 >= len)
                return false;
            p++;
            continue;
        }
        if (src[p] == '(')
            depth++;
        else if (src[p] == ')' && --depth == 0) {
            *close = p;
            return true;
        }
    }
    return false;
}

static const char *direct_callee(AstNode *call, AstNode **ident_out)
{
    AstNode *id;

    if (!call || call->kind != AST_EXPR_CALL)
        return NULL;
    id = written_expr(call->lhs);
    if (!id || id->kind != AST_EXPR_IDENT || !id->name)
        return NULL;
    if (ident_out)
        *ident_out = id;
    return id->name;
}

static bool char_pointer(const Type *type)
{
    return type && type->kind == TY_PTR && type->base &&
           type->base->kind == TY_CHAR;
}

static bool void_pointer(const Type *type)
{
    return type && type->kind == TY_PTR && type->base &&
           type->base->kind == TY_VOID;
}

static bool external_api_signature(const Symbol *sym, const char *name)
{
    const Type *ft;

    if (!sym || sym->kind != SYM_FUNC || sym->linkage != LINK_EXTERNAL ||
        sym->defined || !sym->type || sym->type->kind != TY_FUNC)
        return false;
    ft = sym->type;
    if (!ft->has_proto)
        return false;
    if (strcmp(name, "strcpy") == 0 || strcmp(name, "strcat") == 0)
        return char_pointer(ft->base) && ft->nparams == 2 && !ft->variadic &&
               char_pointer(ft->params[0]) && char_pointer(ft->params[1]);
    if (strcmp(name, "sprintf") == 0)
        return ft->base && ft->base->kind == TY_INT && ft->nparams == 2 &&
               ft->variadic && char_pointer(ft->params[0]) &&
               char_pointer(ft->params[1]);
    if (strcmp(name, "snprintf") == 0)
        return ft->base && ft->base->kind == TY_INT && ft->nparams == 3 &&
               ft->variadic && char_pointer(ft->params[0]) &&
               type_is_integer(ft->params[1]) && char_pointer(ft->params[2]);
    if (strcmp(name, "malloc") == 0)
        return void_pointer(ft->base) && ft->nparams == 1 && !ft->variadic &&
               type_is_integer(ft->params[0]);
    if (strcmp(name, "calloc") == 0 || strcmp(name, "aligned_alloc") == 0)
        return void_pointer(ft->base) && ft->nparams == 2 && !ft->variadic &&
               type_is_integer(ft->params[0]) && type_is_integer(ft->params[1]);
    if (strcmp(name, "realloc") == 0)
        return void_pointer(ft->base) && ft->nparams == 2 && !ft->variadic &&
               void_pointer(ft->params[0]) && type_is_integer(ft->params[1]);
    if (strcmp(name, "reallocarray") == 0)
        return void_pointer(ft->base) && ft->nparams == 3 && !ft->variadic &&
               void_pointer(ft->params[0]) && type_is_integer(ft->params[1]) &&
               type_is_integer(ft->params[2]);
    if (strcmp(name, "strdup") == 0)
        return char_pointer(ft->base) && ft->nparams == 1 && !ft->variadic &&
               char_pointer(ft->params[0]);
    if (strcmp(name, "strndup") == 0)
        return char_pointer(ft->base) && ft->nparams == 2 && !ft->variadic &&
               char_pointer(ft->params[0]) && type_is_integer(ft->params[1]);
    return false;
}

static bool external_api_ident(const AstNode *ident, const char *name)
{
    return ident && external_api_signature(ident->sym, name);
}

static bool external_api_visible(const Autofix *a, const char *name, u32 seq)
{
    const Symbol *sym;

    if (strcmp(name, "snprintf") == 0 &&
        (a->snprintf_shadowed ||
         pp_macro_lookup_at_seq(a->pp, "snprintf", seq)))
        return false;
    for (sym = a->sema->file_scope ? a->sema->file_scope->ordinary : NULL; sym;
         sym = sym->next)
        if (sym->name && strcmp(sym->name, name) == 0)
            return external_api_signature(sym, name);
    return false;
}

static bool call_text(Autofix *a, AstNode *call, AstNode *callee,
                      TextRange *whole, TextRange *args, size_t args_cap,
                      size_t *nargs)
{
    const char *src;
    size_t len, begin, open, close, p, part;
    unsigned parens = 0, brackets = 0, braces = 0;

    if (!span_offset(a->dc, callee->span, &begin, &src, &len) ||
        !span_offset(a->dc, call->span, &open, NULL, NULL) ||
        callee->span.file_id != call->span.file_id ||
        !matching_paren(src, len, open, &close))
        return false;
    whole->src = src;
    whole->len = len;
    whole->begin = begin;
    whole->end = close + 1;
    *nargs = 0;
    part = open + 1;
    for (p = part; p <= close; p++) {
        bool split = p == close;
        char c = p < close ? src[p] : ')';

        if (p < close && (c == '\'' || c == '"')) {
            skip_quoted(src, len, &p, c);
            p--;
            continue;
        }
        if (p < close && c == '/' && p + 1 < close &&
            (src[p + 1] == '/' || src[p + 1] == '*'))
            return false;
        if (p < close) {
            if (c == '(')
                parens++;
            else if (c == ')')
                parens--;
            else if (c == '[')
                brackets++;
            else if (c == ']')
                brackets--;
            else if (c == '{')
                braces++;
            else if (c == '}')
                braces--;
            else if (c == ',' && !parens && !brackets && !braces)
                split = true;
        }
        if (split) {
            size_t b = part, e = p;

            while (b < e && isspace((unsigned char)src[b]))
                b++;
            while (e > b && isspace((unsigned char)src[e - 1]))
                e--;
            if (b != e || p != close) {
                if (*nargs >= args_cap)
                    return false;
                args[*nargs] = (TextRange){src, len, b, e};
                (*nargs)++;
            }
            part = p + 1;
        }
    }
    return true;
}

static char *build_copy_replacement(Autofix *a, const char *name,
                                    const TextRange *args, size_t nargs,
                                    bool is_strcpy)
{
    Buf b;
    char *result;
    size_t i;

    buf_init(&b);
    buf_printf(&b, "snprintf(%s, sizeof %s, ", name, name);
    if (is_strcpy) {
        buf_printf(&b, "\"%%s\", ");
        buf_append(&b, args[1].src + args[1].begin,
                   args[1].end - args[1].begin);
    } else {
        for (i = 1; i < nargs; i++) {
            if (i != 1)
                buf_printf(&b, ", ");
            buf_append(&b, args[i].src + args[i].begin,
                       args[i].end - args[i].begin);
        }
    }
    buf_printf(&b, ")");
    result = arena_strndup(a->sema->arena, (const char *)b.data, b.len);
    buf_free(&b);
    return result;
}

static void check_unbounded_copy(Autofix *a, AstNode *call,
                                 bool value_discarded)
{
    AstNode *callee, *dest;
    const char *fn = direct_callee(call, &callee);
    bool strcpy_call, sprintf_call;
    bool known_array = false;
    TextRange whole, args[16];
    size_t nargs = 0;

    if (!fn || !external_api_ident(callee, fn))
        return;
    strcpy_call = strcmp(fn, "strcpy") == 0;
    sprintf_call = strcmp(fn, "sprintf") == 0;
    if (!strcpy_call && !sprintf_call && strcmp(fn, "strcat") != 0)
        return;
    if (!call->nargs)
        return;
    dest = written_expr(call->args[0]);
    if (dest && dest->kind == AST_EXPR_IDENT && dest->name && dest->sem_type &&
        dest->sem_type->kind == TY_ARRAY && dest->sem_type->has_size)
        known_array = true;

    if (strcmp(fn, "strcat") == 0) {
        warn_at(a->warn, WARN_MEM_UNBOUNDED_COPY, callee->span,
                "unbounded call to 'strcat'%s",
                known_array ? " into a fixed-size array" : "");
        if (warn_enabled(a->warn, WARN_MEM_UNBOUNDED_COPY, callee->span))
            diag_emit(a->dc, DIAG_NOTE, callee->span,
                      "restructure the append with an explicit remaining "
                      "capacity; a mechanical subtraction can underflow");
        return;
    }
    if (!known_array || !value_discarded ||
        !external_api_visible(a, "snprintf", callee->span.seq) ||
        !call_text(a, call, callee, &whole, args, CGF_ARRAY_LEN(args),
                   &nargs) ||
        (strcpy_call && nargs != 2) || (sprintf_call && nargs < 2)) {
        warn_at(a->warn, WARN_MEM_UNBOUNDED_COPY, callee->span,
                "unbounded call to '%s'; no semantics-preserving mechanical "
                "rewrite is proven",
                fn);
        return;
    }
    {
        char *replacement =
            build_copy_replacement(a, dest->name, args, nargs, strcpy_call);
        DiagFixit fix = {{0}, replacement, true};

        fix.where = callee->span;
        fix.where.len = (u32)(whole.end - whole.begin);
        fix.where.origin |= call->span.origin;
        warn_at_fixits(a->warn, WARN_MEM_UNBOUNDED_COPY, callee->span, &fix, 1,
                       "unbounded call to '%s'; use the destination's "
                       "array bound",
                       fn);
    }
}

static bool allocation_call(AstNode *e, AstNode **call_out)
{
    AstNode *callee;
    const char *name;

    e = written_expr(e);
    name = direct_callee(e, &callee);
    if (!name || !external_api_ident(callee, name))
        return false;
    if (strcmp(name, "malloc") != 0 && strcmp(name, "calloc") != 0 &&
        strcmp(name, "realloc") != 0 && strcmp(name, "aligned_alloc") != 0 &&
        strcmp(name, "reallocarray") != 0 && strcmp(name, "strdup") != 0 &&
        strcmp(name, "strndup") != 0)
        return false;
    *call_out = e;
    return true;
}

static bool malloc_size_call(AstNode *e, AstNode **call_out, unsigned *size_arg)
{
    AstNode *callee;
    const char *name;

    e = written_expr(e);
    name = direct_callee(e, &callee);
    if (!name || !external_api_ident(callee, name))
        return false;
    if (strcmp(name, "malloc") == 0) {
        *size_arg = 0;
    } else if (strcmp(name, "calloc") == 0) {
        *size_arg = 1;
    } else if (strcmp(name, "realloc") == 0 ||
               strcmp(name, "aligned_alloc") == 0) {
        *size_arg = 1;
    } else {
        return false;
    }
    if (e->nargs <= *size_arg)
        return false;
    *call_out = e;
    return true;
}

/* Only the sprint's proven allocation shapes are eligible: a direct sizeof,
 * or a count multiplied by exactly one direct sizeof. Never reach into an
 * additive/composite expression and rewrite just one nested term. */
static AstNode *allocation_sizeof(AstNode *e)
{
    AstNode *left, *right;

    e = written_expr(e);
    if (!e)
        return NULL;
    if (e->kind == AST_EXPR_SIZEOF)
        return e;
    if (e->kind != AST_EXPR_BINARY || e->op != PUNCT_STAR)
        return NULL;
    left = written_expr(e->lhs);
    right = written_expr(e->rhs);
    if (left && left->kind == AST_EXPR_SIZEOF &&
        (!right || right->kind != AST_EXPR_SIZEOF))
        return left;
    if (right && right->kind == AST_EXPR_SIZEOF &&
        (!left || left->kind != AST_EXPR_SIZEOF))
        return right;
    return NULL;
}

static bool type_same_unqualified(const Type *a, const Type *b)
{
    u32 i;

    if (a == b)
        return true;
    if (!a || !b || a->kind != b->kind)
        return false;
    switch (a->kind) {
    case TY_PTR:
        return type_same_unqualified(a->base, b->base);
    case TY_ARRAY:
        return type_same_unqualified(a->base, b->base) &&
               (!a->has_size || !b->has_size || a->size == b->size);
    case TY_FUNC:
        if (!type_same_unqualified(a->base, b->base) ||
            a->nparams != b->nparams || a->variadic != b->variadic)
            return false;
        for (i = 0; i < a->nparams; i++)
            if (!type_same_unqualified(a->params[i], b->params[i]))
                return false;
        return true;
    case TY_STRUCT:
    case TY_UNION:
    case TY_ENUM:
        return a->tag == b->tag;
    default:
        return true;
    }
}

static bool sizeof_source_range(Autofix *a, AstNode *sz, Span *where)
{
    const char *src;
    AstNode *operand;
    size_t len, begin, p, close, operand_begin;

    if (!span_offset(a->dc, sz->span, &begin, &src, &len))
        return false;
    p = begin + sz->span.len;
    while (p < len && (src[p] == ' ' || src[p] == '\t'))
        p++;
    if (p < len && src[p] == '(') {
        if (!matching_paren(src, len, p, &close))
            return false;
    } else {
        operand = written_expr(sz->lhs);
        /* Unary/member/index spans name only their leading token in this AST.
         * A primary identifier has a complete token span; decline every
         * richer unparenthesized operand rather than leave a suffix behind. */
        if (!operand || operand->kind != AST_EXPR_IDENT || !operand->span.len ||
            !span_offset(a->dc, operand->span, &operand_begin, NULL, NULL) ||
            operand->span.file_id != sz->span.file_id || operand_begin < p)
            return false;
        close = operand_begin + operand->span.len - 1;
    }
    *where = sz->span;
    where->len = (u32)(close + 1 - begin);
    return true;
}

static void check_sizeof_assignment(Autofix *a, const char *name,
                                    Type *dest_type, AstNode *rhs)
{
    AstNode *call, *sz;
    Type *actual;
    unsigned arg;
    Span where;
    Buf b;
    char *replacement;
    DiagFixit fix;

    if (!name || !dest_type || dest_type->kind != TY_PTR || !dest_type->base ||
        dest_type->base->kind == TY_VOID ||
        !type_is_complete(dest_type->base) ||
        !malloc_size_call(rhs, &call, &arg))
        return;
    sz = allocation_sizeof(call->args[arg]);
    if (!sz)
        return; /* byte-count allocations without sizeof are intentional */
    actual = sz->type ? sema_type_from_ast(a->sema, sz->type, sz->span)
                      : (sz->lhs ? sz->lhs->sem_type : NULL);
    if (!actual || actual->kind == TY_ARRAY || actual->kind == TY_FUNC ||
        type_same_unqualified(actual, dest_type->base))
        return;
    if (!sizeof_source_range(a, sz, &where)) {
        warn_at(a->warn, WARN_MEM_SIZEOF_MISMATCH, sz->span,
                "allocation size does not match the pointee type of '%s'",
                name);
        return;
    }

    buf_init(&b);
    buf_printf(&b, "sizeof *%s", name);
    replacement = arena_strndup(a->sema->arena, (const char *)b.data, b.len);
    buf_free(&b);
    fix = (DiagFixit){where, replacement, true};
    warn_at_fixits(a->warn, WARN_MEM_SIZEOF_MISMATCH, sz->span, &fix, 1,
                   "allocation size does not match the pointee type of '%s'",
                   name);
}

static bool expr_is_name(AstNode *e, const char *name)
{
    e = written_expr(e);
    return e && e->kind == AST_EXPR_IDENT && e->name &&
           strcmp(e->name, name) == 0;
}

static bool expr_is_zero(AstNode *e)
{
    e = written_expr(e);
    if (!e)
        return false;
    if ((e->kind == AST_EXPR_INT || e->kind == AST_EXPR_CHAR) && e->tok)
        return e->tok->int_val == 0;
    if (e->kind == AST_EXPR_CAST)
        return expr_is_zero(e->lhs);
    return false;
}

static bool zero_comparison(AstNode *e, const char *name, u16 op)
{
    e = written_expr(e);
    return e && e->kind == AST_EXPR_BINARY && e->op == op &&
           ((expr_is_name(e->lhs, name) && expr_is_zero(e->rhs)) ||
            (expr_is_zero(e->lhs) && expr_is_name(e->rhs, name)));
}

static bool expr_proves_nonnull(AstNode *e, const char *name)
{
    return expr_is_name(e, name) || zero_comparison(e, name, PUNCT_NOTEQ);
}

static bool expr_proves_null(AstNode *e, const char *name)
{
    e = written_expr(e);
    return (e && e->kind == AST_EXPR_UNARY && e->op == PUNCT_BANG &&
            expr_is_name(e->lhs, name)) ||
           zero_comparison(e, name, PUNCT_EQEQ);
}

static bool expr_mentions_deref(AstNode *e, const char *name)
{
    AstNode *base;
    u32 i;

    if (!e)
        return false;
    if ((e->kind == AST_EXPR_UNARY && e->op == PUNCT_STAR) ||
        (e->kind == AST_EXPR_MEMBER && e->is_arrow) ||
        e->kind == AST_EXPR_INDEX) {
        base = written_expr(e->lhs);
        if (base && base->kind == AST_EXPR_IDENT && base->name &&
            strcmp(base->name, name) == 0)
            return true;
    }
    if (e->kind == AST_EXPR_BINARY && e->op == PUNCT_AMPAMP) {
        if (expr_mentions_deref(e->lhs, name))
            return true;
        return !expr_proves_nonnull(e->lhs, name) &&
               expr_mentions_deref(e->rhs, name);
    }
    if (e->kind == AST_EXPR_BINARY && e->op == PUNCT_PIPEPIPE) {
        if (expr_mentions_deref(e->lhs, name))
            return true;
        return !expr_proves_null(e->lhs, name) &&
               expr_mentions_deref(e->rhs, name);
    }
    if (e->kind == AST_EXPR_COND) {
        if (expr_mentions_deref(e->lhs, name))
            return true;
        if (!expr_proves_nonnull(e->lhs, name) &&
            expr_mentions_deref(e->mid, name))
            return true;
        return !expr_proves_null(e->lhs, name) &&
               expr_mentions_deref(e->rhs, name);
    }
    if (expr_mentions_deref(e->lhs, name) ||
        expr_mentions_deref(e->rhs, name) ||
        expr_mentions_deref(e->mid, name) || expr_mentions_deref(e->init, name))
        return true;
    for (i = 0; i < e->nargs; i++)
        if (expr_mentions_deref(e->args[i], name))
            return true;
    for (i = 0; i < e->nitems; i++)
        if (expr_mentions_deref(e->items[i], name))
            return true;
    return false;
}

static bool stmt_terminates(AstNode *st)
{
    if (!st)
        return false;
    if (st->kind == AST_STMT_RETURN)
        return true;
    return st->kind == AST_STMT_COMPOUND && st->nitems &&
           stmt_terminates(st->items[st->nitems - 1]);
}

static bool is_null_guard(AstNode *st, const char *name)
{
    AstNode *cond, *id;

    if (!st || st->kind != AST_STMT_IF || st->rhs || !stmt_terminates(st->body))
        return false;
    cond = written_expr(st->lhs);
    id = cond && cond->kind == AST_EXPR_UNARY ? written_expr(cond->lhs) : NULL;
    return (id && cond->op == PUNCT_BANG && id->kind == AST_EXPR_IDENT &&
            id->name && strcmp(id->name, name) == 0) ||
           expr_proves_null(cond, name);
}

static bool stmt_reassigns(AstNode *st, const char *name)
{
    AstNode *e, *lhs;

    if (!st || st->kind != AST_STMT_EXPR)
        return false;
    e = written_expr(st->lhs);
    if (!e || e->kind != AST_EXPR_BINARY || e->op != PUNCT_ASSIGN)
        return false;
    lhs = written_expr(e->lhs);
    return lhs && lhs->kind == AST_EXPR_IDENT && lhs->name &&
           strcmp(lhs->name, name) == 0;
}

static bool decl_mentions_deref(AstNode *decl, const char *name)
{
    u32 i;

    if (!decl)
        return false;
    if (expr_mentions_deref(decl->init, name))
        return true;
    for (i = 0; i < decl->nitems; i++)
        if (decl_mentions_deref(decl->items[i], name))
            return true;
    return false;
}

static bool stmt_mentions_deref(AstNode *st, const char *name)
{
    if (!st)
        return false;
    if (st->kind == AST_STMT_EXPR || st->kind == AST_STMT_RETURN)
        return expr_mentions_deref(st->lhs, name);
    if (st->kind == AST_STMT_DECL)
        return decl_mentions_deref(st->lhs, name);
    return false;
}

static bool insertion_after_stmt(Autofix *a, AstNode *st, Span *point)
{
    const char *src;
    size_t len, p, start;

    if (!span_offset(a->dc, st->span, &start, &src, &len))
        return false;
    p = start;
    while (p < len && src[p] != '\n' && src[p] != ';') {
        if (src[p] == '\'' || src[p] == '"') {
            size_t quoted_start = p;

            skip_quoted(src, len, &p, src[p]);
            for (; quoted_start < p; quoted_start++)
                if (src[quoted_start] == '\n')
                    return false;
            continue;
        }
        if (src[p] == '/' && p + 1 < len && src[p + 1] == '/')
            return false;
        if (src[p] == '/' && p + 1 < len && src[p + 1] == '*') {
            p += 2;
            while (p + 1 < len && !(src[p] == '*' && src[p + 1] == '/')) {
                if (src[p] == '\n')
                    return false;
                p++;
            }
            if (p + 1 >= len)
                return false;
            p += 2;
            continue;
        }
        p++;
    }
    if (p >= len || src[p] != ';')
        return false;
    *point = st->span;
    point->col += (u32)(p + 1 - start);
    point->len = 0;
    return true;
}

static void check_null_after_alloc(Autofix *a, AstNode *compound, u32 index,
                                   const char *name, Type *type, AstNode *rhs)
{
    AstNode *call;
    u32 i;

    if (!name || !type || type->kind != TY_PTR || !allocation_call(rhs, &call))
        return;
    (void)call;
    for (i = index + 1; i < compound->nitems; i++) {
        AstNode *st = compound->items[i];

        if (is_null_guard(st, name) || stmt_reassigns(st, name))
            return;
        if (st->kind != AST_STMT_EXPR && st->kind != AST_STMT_DECL &&
            st->kind != AST_STMT_RETURN)
            return; /* do not reason across control-flow joins */
        if (stmt_mentions_deref(st, name)) {
            Span point;
            Buf b;
            char *replacement;
            DiagFixit fix;

            if (!insertion_after_stmt(a, compound->items[index], &point))
                return;
            buf_init(&b);
            buf_printf(&b, "\n%*sif (!%s) { /* handle allocation failure */ }",
                       (int)(compound->items[index]->span.col - 1), "", name);
            replacement =
                arena_strndup(a->sema->arena, (const char *)b.data, b.len);
            buf_free(&b);
            fix = (DiagFixit){point, replacement, false};
            warn_at_fixits(a->warn, WARN_MEM_NULL_CHECK, st->span, &fix, 1,
                           "'%s' is dereferenced before a NULL check", name);
            return;
        }
    }
}

static void visit_expr(Autofix *a, AstNode *e, bool value_discarded);

static bool decl_chain_named(const AstNode *d, const char *name)
{
    u32 i;

    if (!d)
        return false;
    if (d->name && strcmp(d->name, name) == 0)
        return true;
    for (i = 0; i < d->nitems; i++)
        if (decl_chain_named(d->items[i], name))
            return true;
    return false;
}

static void visit_decl(Autofix *a, AstNode *d)
{
    u32 i;

    if (!d)
        return;
    check_sizeof_assignment(a, d->name, d->sem_type, d->init);
    visit_expr(a, d->init, false);
    for (i = 0; i < d->nitems; i++)
        visit_decl(a, d->items[i]);
}

static void visit_expr(Autofix *a, AstNode *e, bool value_discarded)
{
    AstNode *lhs;
    u32 i;

    if (!e || e->unevaluated)
        return;
    if (e->kind == AST_EXPR_CALL)
        check_unbounded_copy(a, e, value_discarded);
    if (e->kind == AST_EXPR_BINARY && e->op == PUNCT_ASSIGN) {
        lhs = written_expr(e->lhs);
        if (lhs && lhs->kind == AST_EXPR_IDENT && lhs->name && lhs->sym)
            check_sizeof_assignment(a, lhs->name, lhs->sym->type, e->rhs);
    }
    visit_expr(a, e->lhs, false);
    visit_expr(a, e->rhs, false);
    visit_expr(a, e->mid, false);
    visit_expr(a, e->init, false);
    for (i = 0; i < e->nargs; i++)
        visit_expr(a, e->args[i], false);
    for (i = 0; i < e->nitems; i++)
        visit_expr(a, e->items[i], false);
}

static void visit_stmt(Autofix *a, AstNode *st)
{
    u32 i;

    if (!st)
        return;
    if (st->kind == AST_STMT_COMPOUND) {
        bool saved_shadow = a->snprintf_shadowed;

        for (i = 0; i < st->nitems; i++) {
            AstNode *item = st->items[i];

            if (item && item->kind == AST_STMT_DECL && item->lhs) {
                AstNode *d = item->lhs;
                u32 sibling = 0;

                while (d) {
                    if (d->name && strcmp(d->name, "snprintf") == 0)
                        a->snprintf_shadowed = true;
                    check_null_after_alloc(a, st, i, d->name, d->sem_type,
                                           d->init);
                    d = item->lhs && sibling < item->lhs->nitems
                            ? item->lhs->items[sibling++]
                            : NULL;
                }
            } else if (item && item->kind == AST_STMT_EXPR) {
                AstNode *assign = written_expr(item->lhs);
                AstNode *lhs;

                if (assign && assign->kind == AST_EXPR_BINARY &&
                    assign->op == PUNCT_ASSIGN) {
                    lhs = written_expr(assign->lhs);
                    if (lhs && lhs->kind == AST_EXPR_IDENT && lhs->sym)
                        check_null_after_alloc(a, st, i, lhs->name,
                                               lhs->sym->type, assign->rhs);
                }
            }
            visit_stmt(a, item);
        }
        a->snprintf_shadowed = saved_shadow;
        return;
    }
    if (st->kind == AST_STMT_DECL) {
        visit_decl(a, st->lhs);
        return;
    }
    if (st->kind == AST_STMT_IF) {
        visit_expr(a, st->lhs, false);
        visit_stmt(a, st->body);
        visit_stmt(a, st->rhs);
        return;
    }
    if (st->kind == AST_STMT_FOR) {
        bool saved_shadow = a->snprintf_shadowed;

        if (st->lhs && st->lhs->kind == AST_DECL &&
            decl_chain_named(st->lhs, "snprintf"))
            a->snprintf_shadowed = true;
        if (st->lhs && st->lhs->kind == AST_DECL)
            visit_decl(a, st->lhs);
        else
            visit_expr(a, st->lhs, true);
        visit_expr(a, st->mid, false);
        visit_expr(a, st->rhs, true);
        visit_stmt(a, st->body);
        a->snprintf_shadowed = saved_shadow;
        return;
    }
    visit_expr(a, st->lhs, st->kind == AST_STMT_EXPR);
    visit_stmt(a, st->body);
}

void memsafe_autofix_translation_unit(WarnCtx *warn, Sema *sema, AstNode *tu,
                                      const Preprocessor *pp)
{
    Autofix a;
    u32 i;

    if (!warn || !sema || !tu || !pp || tu->kind != AST_TRANSLATION_UNIT ||
        !warn_memsafe_autofix_needed(warn))
        return;
    a.warn = warn;
    a.sema = sema;
    a.dc = warn_diag(warn);
    a.pp = pp;
    a.snprintf_shadowed = false;
    for (i = 0; i < tu->ndecls; i++) {
        AstNode *d = tu->decls[i];

        if (!d)
            continue;
        if (d->kind == AST_FUNC_DEF) {
            bool saved_shadow = a.snprintf_shadowed;
            u32 p;

            for (p = 0; p < d->nparam_syms; p++)
                if (d->param_syms[p] && d->param_syms[p]->name &&
                    strcmp(d->param_syms[p]->name, "snprintf") == 0)
                    a.snprintf_shadowed = true;
            visit_stmt(&a, d->body);
            a.snprintf_shadowed = saved_shadow;
        } else if (d->kind == AST_DECL)
            visit_decl(&a, d);
    }
}
