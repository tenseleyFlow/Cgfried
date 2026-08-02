#include "warn/format.h"

#include <string.h>

#include "sema/sema.h"
#include "warn/warn.h"

typedef enum { GATE_ALL, GATE_GNU_LIBC, GATE_FREEBSD } BuiltinGate;

typedef enum {
    BUILTIN_PARAM_ANY,
    BUILTIN_PARAM_INT,
    BUILTIN_PARAM_ULONG,
    BUILTIN_PARAM_PTR,
    BUILTIN_PARAM_CHAR_PTR,
    BUILTIN_PARAM_CHAR_PTR_PTR
} BuiltinParam;

typedef struct {
    const char *name;
    FmtSpec spec;
    BuiltinGate gate;
    TypeKind result;
    u8 fixed_params;
    BuiltinParam params[4];
} BuiltinFmt;

/* These are deliberately the pre-attribute names promised by Sprint 39.
 * The __isoc99 aliases matter in practice: glibc redirects the public scanf
 * spellings before sema sees them.  Sprint 55's explicit format attribute
 * will take precedence over this table. */
static const BuiltinFmt builtin_formats[] = {
    {"__isoc99_fscanf",
     {FMT_SCANF, 2, 3},
     GATE_GNU_LIBC,
     TY_INT,
     2,
     {BUILTIN_PARAM_PTR, BUILTIN_PARAM_CHAR_PTR}},
    {"__isoc99_scanf",
     {FMT_SCANF, 1, 2},
     GATE_GNU_LIBC,
     TY_INT,
     1,
     {BUILTIN_PARAM_CHAR_PTR}},
    {"__isoc99_sscanf",
     {FMT_SCANF, 2, 3},
     GATE_GNU_LIBC,
     TY_INT,
     2,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_CHAR_PTR}},
    {"__isoc99_vfscanf",
     {FMT_SCANF, 2, 0},
     GATE_GNU_LIBC,
     TY_INT,
     3,
     {BUILTIN_PARAM_PTR, BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"__isoc99_vscanf",
     {FMT_SCANF, 1, 0},
     GATE_GNU_LIBC,
     TY_INT,
     2,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"__isoc99_vsscanf",
     {FMT_SCANF, 2, 0},
     GATE_GNU_LIBC,
     TY_INT,
     3,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"asprintf",
     {FMT_PRINTF, 2, 3},
     GATE_GNU_LIBC,
     TY_INT,
     2,
     {BUILTIN_PARAM_CHAR_PTR_PTR, BUILTIN_PARAM_CHAR_PTR}},
    {"dprintf",
     {FMT_PRINTF, 2, 3},
     GATE_ALL,
     TY_INT,
     2,
     {BUILTIN_PARAM_INT, BUILTIN_PARAM_CHAR_PTR}},
    {"err",
     {FMT_PRINTF, 2, 3},
     GATE_FREEBSD,
     TY_VOID,
     2,
     {BUILTIN_PARAM_INT, BUILTIN_PARAM_CHAR_PTR}},
    {"errx",
     {FMT_PRINTF, 2, 3},
     GATE_FREEBSD,
     TY_VOID,
     2,
     {BUILTIN_PARAM_INT, BUILTIN_PARAM_CHAR_PTR}},
    {"fprintf",
     {FMT_PRINTF, 2, 3},
     GATE_ALL,
     TY_INT,
     2,
     {BUILTIN_PARAM_PTR, BUILTIN_PARAM_CHAR_PTR}},
    {"fscanf",
     {FMT_SCANF, 2, 3},
     GATE_ALL,
     TY_INT,
     2,
     {BUILTIN_PARAM_PTR, BUILTIN_PARAM_CHAR_PTR}},
    {"printf",
     {FMT_PRINTF, 1, 2},
     GATE_ALL,
     TY_INT,
     1,
     {BUILTIN_PARAM_CHAR_PTR}},
    {"scanf", {FMT_SCANF, 1, 2}, GATE_ALL, TY_INT, 1, {BUILTIN_PARAM_CHAR_PTR}},
    {"snprintf",
     {FMT_PRINTF, 3, 4},
     GATE_ALL,
     TY_INT,
     3,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ULONG, BUILTIN_PARAM_CHAR_PTR}},
    {"sprintf",
     {FMT_PRINTF, 2, 3},
     GATE_ALL,
     TY_INT,
     2,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_CHAR_PTR}},
    {"sscanf",
     {FMT_SCANF, 2, 3},
     GATE_ALL,
     TY_INT,
     2,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_CHAR_PTR}},
    {"strftime",
     {FMT_STRFTIME, 3, 0},
     GATE_ALL,
     TY_ULONG,
     4,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ULONG, BUILTIN_PARAM_CHAR_PTR,
      BUILTIN_PARAM_PTR}},
    {"strfmon",
     {FMT_STRFMON, 3, 4},
     GATE_ALL,
     TY_LONG,
     3,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ULONG, BUILTIN_PARAM_CHAR_PTR}},
    {"syslog",
     {FMT_PRINTF, 2, 3},
     GATE_GNU_LIBC,
     TY_VOID,
     2,
     {BUILTIN_PARAM_INT, BUILTIN_PARAM_CHAR_PTR}},
    {"vasprintf",
     {FMT_PRINTF, 2, 0},
     GATE_GNU_LIBC,
     TY_INT,
     3,
     {BUILTIN_PARAM_CHAR_PTR_PTR, BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"vdprintf",
     {FMT_PRINTF, 2, 0},
     GATE_ALL,
     TY_INT,
     3,
     {BUILTIN_PARAM_INT, BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"vfprintf",
     {FMT_PRINTF, 2, 0},
     GATE_ALL,
     TY_INT,
     3,
     {BUILTIN_PARAM_PTR, BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"vfscanf",
     {FMT_SCANF, 2, 0},
     GATE_ALL,
     TY_INT,
     3,
     {BUILTIN_PARAM_PTR, BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"vprintf",
     {FMT_PRINTF, 1, 0},
     GATE_ALL,
     TY_INT,
     2,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"vscanf",
     {FMT_SCANF, 1, 0},
     GATE_ALL,
     TY_INT,
     2,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"vsnprintf",
     {FMT_PRINTF, 3, 0},
     GATE_ALL,
     TY_INT,
     4,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ULONG, BUILTIN_PARAM_CHAR_PTR,
      BUILTIN_PARAM_ANY}},
    {"vsprintf",
     {FMT_PRINTF, 2, 0},
     GATE_ALL,
     TY_INT,
     3,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"vsscanf",
     {FMT_SCANF, 2, 0},
     GATE_ALL,
     TY_INT,
     3,
     {BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"vsyslog",
     {FMT_PRINTF, 2, 0},
     GATE_GNU_LIBC,
     TY_VOID,
     3,
     {BUILTIN_PARAM_INT, BUILTIN_PARAM_CHAR_PTR, BUILTIN_PARAM_ANY}},
    {"warn",
     {FMT_PRINTF, 1, 2},
     GATE_FREEBSD,
     TY_VOID,
     1,
     {BUILTIN_PARAM_CHAR_PTR}},
    {"warnx",
     {FMT_PRINTF, 1, 2},
     GATE_FREEBSD,
     TY_VOID,
     1,
     {BUILTIN_PARAM_CHAR_PTR}},
};

typedef enum {
    LEN_NONE,
    LEN_HH,
    LEN_H,
    LEN_L,
    LEN_LL,
    LEN_J,
    LEN_Z,
    LEN_T,
    LEN_CAP_L
} FmtLength;

typedef struct {
    TypeKind kind;
    u8 pointers;
    bool ignore_scalar_sign;
    bool input_pointer;
    bool any_object_pointer;
    const char *name;
} Expected;

typedef enum { ARG_STYLE_NONE, ARG_STYLE_SEQ, ARG_STYLE_NUMBERED } ArgStyle;

typedef struct {
    WarnCtx *warnings;
    Sema *sema;
    const AstNode *call;
    const FmtSpec *spec;
    Span span;
    const u8 *text;
    u32 len;
    u32 next_arg;
    u32 max_numbered;
    ArgStyle style;
    bool mixing_warned;
    bool range_warned;
    bool *used;
    Expected *seen;
    bool *has_seen;
} FormatState;

static bool target_has_gnu_libc(TargetSpec target)
{
    return target.kind == CGF_TARGET_X86_64_LINUX_GNU ||
           target.kind == CGF_TARGET_ARM64_LINUX;
}

static bool target_has_linux_printf_extensions(TargetSpec target)
{
    return target_has_gnu_libc(target) ||
           target.kind == CGF_TARGET_X86_64_LINUX_MUSL;
}

static bool gate_matches(BuiltinGate gate, TargetSpec target)
{
    switch (gate) {
    case GATE_ALL:
        return true;
    case GATE_GNU_LIBC:
        return target_has_gnu_libc(target);
    case GATE_FREEBSD:
        return target.kind == CGF_TARGET_X86_64_FREEBSD;
    }
    return false;
}

bool warn_format_builtin_spec(TargetSpec target, const char *name, FmtSpec *out)
{
    size_t i;

    if (!name)
        return false;
    for (i = 0; i < sizeof(builtin_formats) / sizeof(builtin_formats[0]); i++)
        if (strcmp(name, builtin_formats[i].name) == 0 &&
            gate_matches(builtin_formats[i].gate, target)) {
            if (out)
                *out = builtin_formats[i].spec;
            return true;
        }
    return false;
}

static const BuiltinFmt *builtin_row(TargetSpec target, const char *name)
{
    size_t i;

    for (i = 0; i < sizeof(builtin_formats) / sizeof(builtin_formats[0]); i++)
        if (name && strcmp(name, builtin_formats[i].name) == 0 &&
            gate_matches(builtin_formats[i].gate, target))
            return &builtin_formats[i];
    return NULL;
}

static const AstNode *strip_wrappers(const AstNode *e, bool explicit_casts)
{
    const AstNode *old;

    do {
        old = e;
        while (e && e->kind == AST_EXPR_PAREN)
            e = e->lhs;
        while (e && e->kind == AST_EXPR_CAST && (e->implicit || explicit_casts))
            e = e->lhs;
    } while (e != old);
    return e;
}

static bool char_pointer(const Type *t)
{
    return t && t->kind == TY_PTR && t->base && t->base->kind == TY_CHAR;
}

static bool builtin_param_matches(BuiltinParam expected, const Type *actual)
{
    switch (expected) {
    case BUILTIN_PARAM_ANY:
        return true;
    case BUILTIN_PARAM_INT:
        return actual && actual->kind == TY_INT;
    case BUILTIN_PARAM_ULONG:
        return actual && actual->kind == TY_ULONG;
    case BUILTIN_PARAM_PTR:
        return actual && actual->kind == TY_PTR && actual->base &&
               actual->base->kind != TY_FUNC;
    case BUILTIN_PARAM_CHAR_PTR:
        return char_pointer(actual);
    case BUILTIN_PARAM_CHAR_PTR_PTR:
        return actual && actual->kind == TY_PTR && char_pointer(actual->base);
    }
    return false;
}

static bool rough_signature_matches(const BuiltinFmt *row, const Symbol *symbol)
{
    const Type *ft;
    u32 i;

    if (!row || !symbol || symbol->kind != SYM_FUNC ||
        symbol->linkage != LINK_EXTERNAL || !symbol->type ||
        symbol->type->kind != TY_FUNC)
        return false;
    ft = symbol->type;
    if (!ft->base || ft->base->kind != row->result)
        return false;
    /* An implicit C89 declaration has no prototype.  GCC still applies its
     * builtin knowledge to the standard names, so the return type is the
     * only rough-signature evidence available there. */
    if (!ft->has_proto)
        return true;
    if (ft->nparams != row->fixed_params)
        return false;
    for (i = 0; i < ft->nparams; i++)
        if (!builtin_param_matches(row->params[i], ft->params[i]))
            return false;
    if (row->spec.fmt_arg == 0 || row->spec.fmt_arg > ft->nparams ||
        !char_pointer(ft->params[row->spec.fmt_arg - 1]))
        return false;
    if (row->spec.first_vararg != 0) {
        if (!ft->variadic || ft->nparams + 1 != row->spec.first_vararg)
            return false;
    } else if (ft->variadic) {
        return false;
    }
    return true;
}

static bool ascii_digit(u8 c)
{
    return c >= (u8)'0' && c <= (u8)'9';
}

static u32 read_number(const u8 *text, u32 len, u32 *at)
{
    u32 value = 0;

    while (*at < len && ascii_digit(text[*at])) {
        u32 digit = (u32)(text[*at] - (u8)'0');

        if (value > 1000000u)
            value = 1000001u;
        else
            value = value * 10 + digit;
        (*at)++;
    }
    return value;
}

static bool integer_sign_pair(TypeKind a, TypeKind b)
{
    return (a == TY_SCHAR && b == TY_UCHAR) ||
           (a == TY_UCHAR && b == TY_SCHAR) ||
           (a == TY_SHORT && b == TY_USHORT) ||
           (a == TY_USHORT && b == TY_SHORT) || (a == TY_INT && b == TY_UINT) ||
           (a == TY_UINT && b == TY_INT) || (a == TY_LONG && b == TY_ULONG) ||
           (a == TY_ULONG && b == TY_LONG) ||
           (a == TY_LLONG && b == TY_ULLONG) ||
           (a == TY_ULLONG && b == TY_LLONG);
}

static bool expected_equal(Expected a, Expected b)
{
    return a.kind == b.kind && a.pointers == b.pointers &&
           a.any_object_pointer == b.any_object_pointer;
}

static bool expected_compatible(Expected a, Expected b, bool signedness)
{
    if (expected_equal(a, b))
        return true;
    return !signedness && a.pointers == 0 && b.pointers == 0 &&
           a.ignore_scalar_sign && b.ignore_scalar_sign &&
           integer_sign_pair(a.kind, b.kind);
}

static bool expected_matches(const Expected *expected, const Type *actual,
                             bool signedness)
{
    u8 depth;

    if (!expected || !actual)
        return true;
    if (expected->any_object_pointer)
        return actual->kind == TY_PTR && actual->base &&
               actual->base->kind != TY_FUNC;
    for (depth = 0; depth < expected->pointers; depth++) {
        if (!actual || actual->kind != TY_PTR)
            return false;
        actual = actual->base;
        if (!expected->input_pointer && actual && actual->quals)
            return false;
    }
    if (!actual || actual->kind != expected->kind)
        if (!(expected->pointers == 0 && expected->ignore_scalar_sign &&
              !signedness && actual &&
              integer_sign_pair(expected->kind, actual->kind)))
            return false;
    return true;
}

static void directive_text(char out[64], const u8 *text, u32 begin, u32 end)
{
    u32 n = end > begin ? end - begin : 0;

    if (n > 62)
        n = 62;
    if (n)
        memcpy(out, text + begin, n);
    out[n] = '\0';
}

static void warn_missing(FormatState *st, Expected expected, const char *dir)
{
    warn_at(st->warnings, WARN_FORMAT, st->span,
            "format '%s' expects a matching '%s' argument", dir, expected.name);
}

static void warn_mismatch(FormatState *st, Expected expected, const char *dir,
                          u32 absolute, const Type *actual, WarnId id)
{
    warn_at(st->warnings, id, st->span,
            "format '%s' expects argument of type '%s', but argument %u "
            "has type '%s'",
            dir, expected.name, (unsigned)(absolute + 1),
            type_to_str(st->sema->arena, actual));
}

/* position is relative to the first variadic argument and one-based; zero
 * denotes the next sequential argument. */
static const AstNode *claim_arg(FormatState *st, u32 position,
                                Expected expected, const char *dir)
{
    u32 absolute;
    bool numbered = position != 0;
    bool signedness =
        warn_enabled(st->warnings, WARN_FORMAT_SIGNEDNESS, st->span);

    if (st->spec->first_vararg == 0)
        return NULL;
    if (numbered) {
        if (st->style == ARG_STYLE_SEQ && !st->mixing_warned) {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "$ operand number used after format without operand "
                    "number");
            st->mixing_warned = true;
        }
        st->style = ARG_STYLE_NUMBERED;
        if (position == 0 || position > 1000000u) {
            if (!st->range_warned)
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "operand number out of range in format");
            st->range_warned = true;
            return NULL;
        }
        absolute = (u32)st->spec->first_vararg - 1 + position - 1;
        if (position > st->max_numbered)
            st->max_numbered = position;
    } else {
        if (st->style == ARG_STYLE_NUMBERED && !st->mixing_warned) {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "missing $ operand number in format");
            st->mixing_warned = true;
        }
        if (st->style == ARG_STYLE_NUMBERED) {
            u32 after_numbered =
                (u32)st->spec->first_vararg - 1 + st->max_numbered;

            if (st->next_arg < after_numbered)
                st->next_arg = after_numbered;
        }
        if (st->style == ARG_STYLE_NONE)
            st->style = ARG_STYLE_SEQ;
        absolute = st->next_arg++;
    }
    if (absolute >= st->call->nargs) {
        if (numbered) {
            if (!st->range_warned)
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "operand number out of range in format");
            st->range_warned = true;
        } else {
            warn_missing(st, expected, dir);
        }
        return NULL;
    }
    st->used[absolute] = true;
    if (st->has_seen[absolute] &&
        !expected_compatible(st->seen[absolute], expected, signedness)) {
        WarnId id =
            expected.pointers == 0 && st->seen[absolute].pointers == 0 &&
                    expected.ignore_scalar_sign &&
                    st->seen[absolute].ignore_scalar_sign &&
                    integer_sign_pair(st->seen[absolute].kind, expected.kind)
                ? WARN_FORMAT_SIGNEDNESS
                : WARN_FORMAT;

        warn_mismatch(st, expected, dir, absolute,
                      st->call->args[absolute]->sem_type, id);
        return st->call->args[absolute];
    }
    st->seen[absolute] = expected;
    st->has_seen[absolute] = true;
    if (!expected_matches(&expected, st->call->args[absolute]->sem_type,
                          signedness)) {
        const Type *actual = st->call->args[absolute]->sem_type;
        WarnId id = expected.pointers == 0 && expected.ignore_scalar_sign &&
                            actual &&
                            integer_sign_pair(expected.kind, actual->kind)
                        ? WARN_FORMAT_SIGNEDNESS
                        : WARN_FORMAT;

        warn_mismatch(st, expected, dir, absolute, actual, id);
    } else if (expected.any_object_pointer && st->sema->lang->pedantic &&
               st->call->args[absolute]->sem_type->base->kind != TY_VOID) {
        warn_at(
            st->warnings, WARN_FORMAT, st->span,
            "format '%s' expects argument of type 'void *', but "
            "argument %u has type '%s'",
            dir, (unsigned)(absolute + 1),
            type_to_str(st->sema->arena, st->call->args[absolute]->sem_type));
    }
    return st->call->args[absolute];
}

static void consume_unchecked_arg(FormatState *st, u32 position)
{
    u32 absolute;

    if (st->spec->first_vararg == 0)
        return;
    if (position) {
        absolute = (u32)st->spec->first_vararg - 1 + position - 1;
        if (position > st->max_numbered)
            st->max_numbered = position;
        st->style = ARG_STYLE_NUMBERED;
    } else {
        if (st->style == ARG_STYLE_NUMBERED) {
            u32 after_numbered =
                (u32)st->spec->first_vararg - 1 + st->max_numbered;

            if (st->next_arg < after_numbered)
                st->next_arg = after_numbered;
        }
        if (st->style == ARG_STYLE_NONE)
            st->style = ARG_STYLE_SEQ;
        absolute = st->next_arg++;
    }
    if (absolute < st->call->nargs)
        st->used[absolute] = true;
}

static u32 parse_position(const u8 *text, u32 len, u32 *at)
{
    u32 save = *at;
    u32 number;

    if (*at >= len || !ascii_digit(text[*at]))
        return 0;
    number = read_number(text, len, at);
    if (*at < len && text[*at] == (u8)'$') {
        (*at)++;
        return number ? number : 1000001u;
    }
    *at = save;
    return 0;
}

static FmtLength parse_length(const u8 *text, u32 len, u32 *at)
{
    if (*at + 1 < len && text[*at] == (u8)'h' && text[*at + 1] == (u8)'h') {
        *at += 2;
        return LEN_HH;
    }
    if (*at + 1 < len && text[*at] == (u8)'l' && text[*at + 1] == (u8)'l') {
        *at += 2;
        return LEN_LL;
    }
    if (*at >= len)
        return LEN_NONE;
    switch (text[*at]) {
    case 'h':
        (*at)++;
        return LEN_H;
    case 'l':
        (*at)++;
        return LEN_L;
    case 'q':
        (*at)++;
        return LEN_LL;
    case 'j':
        (*at)++;
        return LEN_J;
    case 'z':
    case 'Z':
        (*at)++;
        return LEN_Z;
    case 't':
        (*at)++;
        return LEN_T;
    case 'L':
        (*at)++;
        return LEN_CAP_L;
    default:
        return LEN_NONE;
    }
}

static Expected scalar(TypeKind kind, const char *name, bool ignore_sign)
{
    Expected e = {kind, 0, ignore_sign, false, false, name};

    return e;
}

static Expected pointer(TypeKind kind, u8 depth, const char *name, bool input)
{
    Expected e = {kind, depth, false, input, false, name};

    return e;
}

static bool printf_integer_expected(u8 conv, FmtLength length, Expected *out)
{
    bool uns = conv == (u8)'o' || conv == (u8)'u' || conv == (u8)'x' ||
               conv == (u8)'X';

    switch (length) {
    case LEN_NONE:
    case LEN_HH:
    case LEN_H:
        *out =
            scalar(uns ? TY_UINT : TY_INT, uns ? "unsigned int" : "int", true);
        /* uchar/ushort promote to int on every v0.1.0 target. */
        if (uns && length != LEN_NONE) {
            out->kind = TY_INT;
            out->name = "int";
        }
        return true;
    case LEN_L:
        *out = scalar(uns ? TY_ULONG : TY_LONG, uns ? "unsigned long" : "long",
                      true);
        return true;
    case LEN_LL:
        *out = scalar(uns ? TY_ULLONG : TY_LLONG,
                      uns ? "unsigned long long" : "long long", true);
        return true;
    case LEN_J:
        *out = scalar(uns ? TY_ULONG : TY_LONG, uns ? "uintmax_t" : "intmax_t",
                      true);
        return true;
    case LEN_Z:
        *out =
            scalar(uns ? TY_ULONG : TY_LONG, uns ? "size_t" : "ssize_t", true);
        return true;
    case LEN_T:
        *out = scalar(uns ? TY_ULONG : TY_LONG,
                      uns ? "unsigned long" : "ptrdiff_t", true);
        return true;
    default:
        return false;
    }
}

static TypeKind wchar_kind(const Sema *s)
{
    return s->target.kind == CGF_TARGET_ARM64_LINUX ? TY_UINT : TY_INT;
}

static bool printf_expected(const Sema *s, u8 conv, FmtLength length,
                            Expected *out)
{
    if (strchr("diouxX", (int)conv))
        return printf_integer_expected(conv, length, out);
    if (strchr("eEfFgGaA", (int)conv)) {
        if (length == LEN_NONE || length == LEN_L) {
            *out = scalar(TY_DOUBLE, "double", false);
            return true;
        }
        if (length == LEN_CAP_L) {
            *out = scalar(TY_LDOUBLE, "long double", false);
            return true;
        }
        return false;
    }
    if (conv == (u8)'c') {
        if (length == LEN_NONE) {
            *out = scalar(TY_INT, "int", true);
            return true;
        }
        if (length == LEN_L) {
            *out = scalar(TY_UINT, "wint_t", true);
            return true;
        }
        return false;
    }
    if (conv == (u8)'s') {
        if (length == LEN_NONE) {
            *out = pointer(TY_CHAR, 1, "char *", true);
            return true;
        }
        if (length == LEN_L) {
            *out = pointer(wchar_kind(s), 1, "wchar_t *", true);
            return true;
        }
        return false;
    }
    if (conv == (u8)'p' && length == LEN_NONE) {
        *out = pointer(TY_VOID, 1, "void *", true);
        out->any_object_pointer = true;
        return true;
    }
    if (conv == (u8)'n') {
        TypeKind kind;
        const char *name;

        switch (length) {
        case LEN_NONE:
            kind = TY_INT;
            name = "int *";
            break;
        case LEN_HH:
            kind = TY_SCHAR;
            name = "signed char *";
            break;
        case LEN_H:
            kind = TY_SHORT;
            name = "short *";
            break;
        case LEN_L:
            kind = TY_LONG;
            name = "long *";
            break;
        case LEN_LL:
            kind = TY_LLONG;
            name = "long long *";
            break;
        case LEN_J:
            kind = TY_LONG;
            name = "intmax_t *";
            break;
        case LEN_Z:
            kind = TY_LONG;
            name = "ssize_t *";
            break;
        case LEN_T:
            kind = TY_LONG;
            name = "ptrdiff_t *";
            break;
        default:
            return false;
        }
        *out = pointer(kind, 1, name, false);
        return true;
    }
    return false;
}

static bool scanf_expected(const Sema *s, u8 conv, FmtLength length,
                           bool allocation, Expected *out)
{
    bool uns = strchr("ouxX", (int)conv) != NULL;
    TypeKind kind;
    const char *name;

    if (strchr("diouxXn", (int)conv)) {
        switch (length) {
        case LEN_NONE:
            kind = uns ? TY_UINT : TY_INT;
            name = uns ? "unsigned int *" : "int *";
            break;
        case LEN_HH:
            kind = uns ? TY_UCHAR : TY_SCHAR;
            name = uns ? "unsigned char *" : "signed char *";
            break;
        case LEN_H:
            kind = uns ? TY_USHORT : TY_SHORT;
            name = uns ? "unsigned short *" : "short *";
            break;
        case LEN_L:
            kind = uns ? TY_ULONG : TY_LONG;
            name = uns ? "unsigned long *" : "long *";
            break;
        case LEN_LL:
            kind = uns ? TY_ULLONG : TY_LLONG;
            name = uns ? "unsigned long long *" : "long long *";
            break;
        case LEN_J:
            kind = uns ? TY_ULONG : TY_LONG;
            name = uns ? "uintmax_t *" : "intmax_t *";
            break;
        case LEN_Z:
            kind = uns ? TY_ULONG : TY_LONG;
            name = uns ? "size_t *" : "ssize_t *";
            break;
        case LEN_T:
            kind = uns ? TY_ULONG : TY_LONG;
            name = uns ? "unsigned long *" : "ptrdiff_t *";
            break;
        default:
            return false;
        }
        *out = pointer(kind, 1, name, false);
        return true;
    }
    if (strchr("eEfFgGaA", (int)conv)) {
        if (length == LEN_NONE) {
            *out = pointer(TY_FLOAT, 1, "float *", false);
            return true;
        }
        if (length == LEN_L) {
            *out = pointer(TY_DOUBLE, 1, "double *", false);
            return true;
        }
        if (length == LEN_CAP_L) {
            *out = pointer(TY_LDOUBLE, 1, "long double *", false);
            return true;
        }
        return false;
    }
    if (conv == (u8)'c' || conv == (u8)'s' || conv == (u8)'[') {
        if (length == LEN_NONE)
            *out = pointer(TY_CHAR, allocation ? 2 : 1,
                           allocation ? "char **" : "char *", false);
        else if (length == LEN_L)
            *out = pointer(wchar_kind(s), allocation ? 2 : 1,
                           allocation ? "wchar_t **" : "wchar_t *", false);
        else
            return false;
        return true;
    }
    if (conv == (u8)'p' && length == LEN_NONE) {
        *out = pointer(TY_VOID, 2, "void **", false);
        return true;
    }
    return false;
}

static void finish_arguments(FormatState *st)
{
    u32 first, i;

    if (st->spec->first_vararg == 0)
        return;
    first = (u32)st->spec->first_vararg - 1;
    if (st->style == ARG_STYLE_NUMBERED) {
        if (!st->range_warned)
            for (i = 0; i < st->max_numbered; i++)
                if (first + i >= st->call->nargs || !st->used[first + i]) {
                    warn_at(st->warnings, WARN_FORMAT, st->span,
                            "format argument %u unused before used argument "
                            "%u in $-style format",
                            (unsigned)(i + 1), (unsigned)st->max_numbered);
                    break;
                }
        i = first + st->max_numbered;
        if (i < st->next_arg)
            i = st->next_arg;
    } else {
        i = st->next_arg;
    }
    if (i < st->call->nargs)
        warn_at(st->warnings, WARN_FORMAT_EXTRA_ARGS, st->span,
                "too many arguments for format");
}

/* GCC 8's print_char_table encodes these as compact flag strings.  Keeping
 * the same table shape here makes every flag/conversion decision explicit:
 * w = field width and p = precision. */
static const char *printf_allowed(u8 conv)
{
    if (conv == (u8)'d' || conv == (u8)'i')
        return "-wp0 +'I";
    if (conv == (u8)'o' || conv == (u8)'x' || conv == (u8)'X')
        return "-wp0#";
    if (conv == (u8)'u')
        return "-wp0'I";
    if (conv == (u8)'f' || conv == (u8)'g' || conv == (u8)'G' ||
        conv == (u8)'F')
        return "-wp0 +#'I";
    if (conv == (u8)'e' || conv == (u8)'E')
        return "-wp0 +#I";
    if (conv == (u8)'a' || conv == (u8)'A')
        return "-wp0 +#";
    if (conv == (u8)'c' || conv == (u8)'p')
        return "-w";
    if (conv == (u8)'s' || conv == (u8)'m')
        return "-wp";
    if (conv == (u8)'n' || conv == (u8)'%')
        return "";
    return NULL;
}

static void check_printf_shape(FormatState *st, const bool flags[256],
                               bool has_width, bool has_precision, u8 conv,
                               const char *dir)
{
    static const char flag_chars[] = " #0-+'I";
    const char *allowed = printf_allowed(conv);
    size_t i;

    if (!allowed)
        return;
    for (i = 0; i < sizeof(flag_chars) - 1; i++) {
        u8 flag = (u8)flag_chars[i];

        if (flags[flag] && !strchr(allowed, (int)flag))
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "'%c' flag used with '%s' format", flag, dir);
    }
    if (has_width && !strchr(allowed, 'w'))
        warn_at(st->warnings, WARN_FORMAT, st->span,
                "field width used with '%s' format", dir);
    if (has_precision && !strchr(allowed, 'p'))
        warn_at(st->warnings, WARN_FORMAT, st->span,
                "precision used with '%s' format", dir);
}

static void check_printf(FormatState *st)
{
    u32 at = 0;

    while (at < st->len) {
        u32 begin, position, flags_at;
        bool flags[256] = {false};
        bool has_width = false, has_precision = false;
        FmtLength length;
        Expected expected;
        u8 conv;
        char dir[64];

        if (st->text[at++] != (u8)'%')
            continue;
        begin = at - 1;
        if (at == st->len) {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "spurious trailing '%%' in format");
            break;
        }
        position = parse_position(st->text, st->len, &at);
        flags_at = at;
        while (at < st->len && strchr("#0- +'I", (int)st->text[at])) {
            u8 flag = st->text[at++];

            if (flags[flag])
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "repeated '%c' flag in format", flag);
            flags[flag] = true;
        }
        (void)flags_at;
        if (at < st->len && st->text[at] == (u8)'*') {
            u32 width_pos;
            Expected width = scalar(TY_INT, "int", true);

            at++;
            width_pos = parse_position(st->text, st->len, &at);
            directive_text(dir, st->text, begin, at);
            (void)claim_arg(st, width_pos, width, dir);
            has_width = true;
        } else if (at < st->len && ascii_digit(st->text[at])) {
            (void)read_number(st->text, st->len, &at);
            has_width = true;
        }
        if (at < st->len && st->text[at] == (u8)'.') {
            has_precision = true;
            at++;
            if (at < st->len && st->text[at] == (u8)'*') {
                u32 precision_pos;
                Expected precision = scalar(TY_INT, "int", true);

                at++;
                precision_pos = parse_position(st->text, st->len, &at);
                directive_text(dir, st->text, begin, at);
                (void)claim_arg(st, precision_pos, precision, dir);
            } else {
                (void)read_number(st->text, st->len, &at);
            }
        }
        length = parse_length(st->text, st->len, &at);
        if (at == st->len) {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "spurious trailing '%%' in format");
            break;
        }
        conv = st->text[at++];
        directive_text(dir, st->text, begin, at);
        if ((conv == (u8)'C' || conv == (u8)'S') &&
            target_has_gnu_libc(st->sema->target)) {
            if (st->sema->lang->pedantic)
                warn_pedwarn_at(st->warnings, WARN_FORMAT, st->span,
                                "ISO C does not support the '%c' format", conv);
            length = LEN_L;
            conv = conv == (u8)'C' ? (u8)'c' : (u8)'s';
        }
        if (conv == (u8)'m' &&
            target_has_linux_printf_extensions(st->sema->target)) {
            if (length != LEN_NONE)
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "length modifier used with '%s' format", dir);
            check_printf_shape(st, flags, has_width, has_precision, conv, dir);
            continue;
        }
        if (conv == (u8)'%') {
            if (position || length != LEN_NONE || has_width || has_precision ||
                at - flags_at != 1)
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "conversion lacks type at end of format");
            continue;
        }
        if (!printf_expected(st->sema, conv, length, &expected)) {
            if (!strchr("diouxXeEfFgGaAcspn", (int)conv))
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "unknown conversion type character '%c' in format",
                        conv);
            else
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "length modifier in format '%s'", dir);
            if (strchr("diouxXeEfFgGaAcspn", (int)conv))
                consume_unchecked_arg(st, position);
            continue;
        }
        if (!target_has_linux_printf_extensions(st->sema->target) &&
            flags[(u8)'\''])
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "''' flag is not supported for this target");
        if (!target_has_gnu_libc(st->sema->target) && flags[(u8)'I'])
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "'I' flag is not supported for this target");
        check_printf_shape(st, flags, has_width, has_precision, conv, dir);
        (void)claim_arg(st, position, expected, dir);
    }
    finish_arguments(st);
}

static bool fixed_array_argument(const AstNode *arg)
{
    arg = strip_wrappers(arg, false);
    return arg && arg->sem_type && arg->sem_type->kind == TY_ARRAY &&
           arg->sem_type->has_size;
}

static void check_scanf(FormatState *st)
{
    u32 at = 0;

    while (at < st->len) {
        u32 begin, position, width = 0;
        bool suppress = false, allocation = false;
        FmtLength length;
        Expected expected;
        const AstNode *actual;
        u8 conv;
        char dir[64];

        if (st->text[at++] != (u8)'%')
            continue;
        begin = at - 1;
        if (at == st->len) {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "spurious trailing '%%' in format");
            break;
        }
        position = parse_position(st->text, st->len, &at);
        if (at < st->len && st->text[at] == (u8)'*') {
            suppress = true;
            at++;
        }
        if (at < st->len && ascii_digit(st->text[at]))
            width = read_number(st->text, st->len, &at);
        if (at < st->len && st->text[at] == (u8)'m') {
            allocation = true;
            at++;
        }
        length = parse_length(st->text, st->len, &at);
        if (at == st->len) {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "spurious trailing '%%' in format");
            break;
        }
        conv = st->text[at++];
        if (conv == (u8)'[') {
            if (at < st->len && st->text[at] == (u8)'^')
                at++;
            if (at < st->len && st->text[at] == (u8)']')
                at++;
            while (at < st->len && st->text[at] != (u8)']')
                at++;
            if (at == st->len) {
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "no closing ']' bracket in scanf format");
                return;
            }
            at++;
        }
        directive_text(dir, st->text, begin, at);
        if ((conv == (u8)'C' || conv == (u8)'S') &&
            target_has_gnu_libc(st->sema->target)) {
            if (st->sema->lang->pedantic)
                warn_pedwarn_at(st->warnings, WARN_FORMAT, st->span,
                                "ISO C does not support the '%c' format", conv);
            length = LEN_L;
            conv = conv == (u8)'C' ? (u8)'c' : (u8)'s';
        }
        if (conv == (u8)'%') {
            if (position || suppress || width || allocation ||
                length != LEN_NONE) {
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "conversion lacks type at end of format");
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "spurious trailing '%%' in format");
            }
            continue;
        }
        if (suppress && length != LEN_NONE)
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "use of assignment suppression and length modifier "
                    "together in scanf format");
        if (allocation && conv != (u8)'c' && conv != (u8)'s' && conv != (u8)'[')
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "'m' flag used with '%s' format", dir);
        if (!scanf_expected(st->sema, conv, length, allocation, &expected)) {
            if (!strchr("diouxXeEfFgGaAcspn[", (int)conv))
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "unknown conversion type character '%c' in format",
                        conv);
            else
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "length modifier in format '%s'", dir);
            if (strchr("diouxXeEfFgGaAcspn[", (int)conv))
                consume_unchecked_arg(st, position);
            continue;
        }
        if (suppress)
            continue;
        actual = claim_arg(st, position, expected, dir);
        if (actual && width == 0 && (conv == (u8)'s' || conv == (u8)'[') &&
            !allocation && fixed_array_argument(actual))
            warn_at(st->warnings, WARN_FORMAT_UNBOUNDED_SCANF, st->span,
                    "unbounded scanf conversion into a fixed-size array");
    }
    finish_arguments(st);
}

static void check_strftime(FormatState *st)
{
    u32 at = 0;

    while (at < st->len) {
        bool flags[256] = {false};
        bool has_width = false;
        u8 modifier = 0;
        u8 conv;
        const char *allowed_flags = "";
        const char *allowed_modifiers = "";
        bool valid = true;
        size_t i;

        if (st->text[at++] != (u8)'%')
            continue;
        if (at == st->len) {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "spurious trailing '%%' in format");
            break;
        }
        while (at < st->len && strchr("_-0^#", (int)st->text[at])) {
            flags[st->text[at]] = true;
            at++;
        }
        while (at < st->len && ascii_digit(st->text[at])) {
            has_width = true;
            at++;
        }
        if (at < st->len &&
            (st->text[at] == (u8)'E' || st->text[at] == (u8)'O')) {
            modifier = st->text[at++];
        }
        if (at == st->len) {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "spurious trailing '%%' in format");
            break;
        }
        conv = st->text[at++];
        if (strchr("ABZab", (int)conv))
            allowed_flags = "^#";
        else if (conv == (u8)'c' || conv == (u8)'x')
            allowed_modifiers = "E";
        else if (strchr("HIMSUWdmw", (int)conv)) {
            allowed_flags = "-_0";
            allowed_modifiers = "O";
        } else if (conv == (u8)'j') {
            allowed_flags = "-_0";
            allowed_modifiers = "O";
        } else if (conv == (u8)'p')
            allowed_flags = "#";
        else if (conv == (u8)'X')
            allowed_modifiers = "E";
        else if (conv == (u8)'y' || conv == (u8)'Y' || conv == (u8)'C') {
            allowed_flags = "-_0";
            allowed_modifiers = "EO";
        } else if (conv == (u8)'D' || strchr("FRTnrt", (int)conv) ||
                   conv == (u8)'%') {
            /* No flags or modifiers. */
        } else if (strchr("eVu", (int)conv) || conv == (u8)'g' ||
                   conv == (u8)'G' || conv == (u8)'z') {
            allowed_flags = "-_0";
            allowed_modifiers = "O";
        } else if (conv == (u8)'h')
            allowed_flags = "^#";
        else if (target_has_gnu_libc(st->sema->target) &&
                 strchr("kls", (int)conv)) {
            allowed_flags = "-_0";
            allowed_modifiers = "O";
        } else if (target_has_gnu_libc(st->sema->target) && conv == (u8)'P') {
            /* GNU extension with no flags or modifiers. */
        } else {
            valid = false;
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "unknown conversion type character '%c' in format", conv);
        }
        if (!valid)
            continue;
        if (!target_has_gnu_libc(st->sema->target) &&
            (flags[(u8)'_'] || flags[(u8)'-'] || flags[(u8)'0'] ||
             flags[(u8)'^'] || flags[(u8)'#'] || has_width) &&
            st->sema->lang->pedantic)
            warn_pedwarn_at(st->warnings, WARN_FORMAT, st->span,
                            "ISO C does not support strftime flags or width");
        if (target_has_gnu_libc(st->sema->target)) {
            static const char flag_chars[] = "_-0^#";

            for (i = 0; i < sizeof(flag_chars) - 1; i++)
                if (flags[(u8)flag_chars[i]] &&
                    !strchr(allowed_flags, flag_chars[i]))
                    warn_at(st->warnings, WARN_FORMAT, st->span,
                            "'%c' flag used with '%%%c' format", flag_chars[i],
                            conv);
            if (has_width && !strchr("HIMSUWdmwjyYCegGzVukls", (int)conv))
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "field width used with '%%%c' format", conv);
        }
        if (modifier && !strchr(allowed_modifiers, (int)modifier))
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "'%c' modifier used with '%%%c' format", modifier, conv);
        if (conv == (u8)'y' || conv == (u8)'g' || conv == (u8)'D' ||
            conv == (u8)'c' || conv == (u8)'x' || (modifier && conv == (u8)'y'))
            warn_at(st->warnings, WARN_FORMAT_Y2K, st->span,
                    "format may yield only last 2 digits of year");
    }
}

static void check_strfmon(FormatState *st)
{
    u32 at = 0;

    while (at < st->len) {
        u32 begin;
        FmtLength length = LEN_NONE;
        Expected expected;
        char dir[64];
        u8 conv;
        bool decorated = false;
        bool flags[256] = {false};

        if (st->text[at++] != (u8)'%')
            continue;
        begin = at - 1;
        if (at == st->len) {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "spurious trailing '%%' in format");
            break;
        }
        for (;;) {
            if (at < st->len && strchr("^(-+!", (int)st->text[at])) {
                u8 flag = st->text[at];

                decorated = true;
                if (flags[flag])
                    warn_at(st->warnings, WARN_FORMAT, st->span,
                            "repeated '%c' flag in format", flag);
                flags[flag] = true;
                at++;
                continue;
            }
            if (at < st->len && st->text[at] == (u8)'=') {
                decorated = true;
                if (flags[(u8)'='])
                    warn_at(st->warnings, WARN_FORMAT, st->span,
                            "repeated fill character in format");
                flags[(u8)'='] = true;
                at++;
                if (at == st->len) {
                    warn_at(st->warnings, WARN_FORMAT, st->span,
                            "missing fill character at end of strfmon format");
                    return;
                }
                at++;
                continue;
            }
            break;
        }
        while (at < st->len && ascii_digit(st->text[at])) {
            decorated = true;
            at++;
        }
        if (at < st->len && st->text[at] == (u8)'#') {
            decorated = true;
            at++;
            if (at == st->len || !ascii_digit(st->text[at]))
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "empty left precision in strfmon format");
            while (at < st->len && ascii_digit(st->text[at]))
                at++;
        }
        if (at < st->len && st->text[at] == (u8)'.') {
            decorated = true;
            at++;
            if (at == st->len || !ascii_digit(st->text[at]))
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "empty precision in strfmon format");
            while (at < st->len && ascii_digit(st->text[at]))
                at++;
        }
        if (at < st->len && st->text[at] == (u8)'L') {
            decorated = true;
            length = LEN_CAP_L;
            at++;
        }
        if (at == st->len) {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "spurious trailing '%%' in format");
            break;
        }
        conv = st->text[at++];
        if (conv == (u8)'%') {
            if (decorated) {
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "conversion lacks type at end of format");
                warn_at(st->warnings, WARN_FORMAT, st->span,
                        "spurious trailing '%%' in format");
            }
            continue;
        }
        directive_text(dir, st->text, begin, at);
        if (conv != (u8)'i' && conv != (u8)'n') {
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "unknown conversion type character '%c' in format", conv);
            continue;
        }
        if (flags[(u8)'+'] && flags[(u8)'('])
            warn_at(st->warnings, WARN_FORMAT, st->span,
                    "use of '+' flag and '(' flag together in strfmon format");
        expected = length == LEN_CAP_L
                       ? scalar(TY_LDOUBLE, "long double", false)
                       : scalar(TY_DOUBLE, "double", false);
        (void)claim_arg(st, 0, expected, dir);
    }
    finish_arguments(st);
}

static const AstNode *format_plain(const AstNode *arg)
{
    const AstNode *plain = strip_wrappers(arg, false);

    if (plain && plain->kind == AST_EXPR_CAST && !plain->implicit &&
        char_pointer(plain->sem_type))
        plain = strip_wrappers(plain->lhs, true);
    return plain;
}

static bool all_literal_formats(const AstNode *arg)
{
    const AstNode *plain = format_plain(arg);

    if (plain && plain->kind == AST_EXPR_COND) {
        return all_literal_formats(plain->mid) &&
               all_literal_formats(plain->rhs);
    }
    if (plain && plain->kind == AST_EXPR_GENERIC && plain->mid)
        return all_literal_formats(plain->mid);
    return plain && plain->kind == AST_EXPR_STRING && plain->tok &&
           plain->tok->str.enc == ENC_NONE;
}

static u32 literal_format_count(const AstNode *arg)
{
    const AstNode *plain = format_plain(arg);

    if (plain && plain->kind == AST_EXPR_COND)
        return literal_format_count(plain->mid) +
               literal_format_count(plain->rhs);
    if (plain && plain->kind == AST_EXPR_GENERIC && plain->mid)
        return literal_format_count(plain->mid);
    return 1;
}

static bool null_pointer_constant(Sema *s, const AstNode *arg)
{
    ConstValue value;
    const AstNode *plain = strip_wrappers(arg, true);

    if (!plain)
        return false;
    value = constexpr_eval(s, (AstNode *)plain, CE_FOLD);
    return value.kind == CV_INT && value.i == 0;
}

static void check_literal_format(WarnCtx *w, Sema *s, const AstNode *call,
                                 const FmtSpec *spec, const AstNode *format)
{
    FormatState state;
    u32 parse_len;
    const u8 *nul;

    parse_len = format->tok->str.nbytes;
    nul = memchr(format->tok->str.bytes, 0, parse_len);
    if (nul) {
        warn_at(w, WARN_FORMAT_CONTAINS_NUL, format->span,
                "embedded '\\0' in format");
        parse_len = (u32)(nul - format->tok->str.bytes);
    }
    if (parse_len == 0)
        warn_at(w, WARN_FORMAT_ZERO_LENGTH, format->span,
                "zero-length format string");

    memset(&state, 0, sizeof(state));
    state.warnings = w;
    state.sema = s;
    state.call = call;
    state.spec = spec;
    state.span = format->span;
    state.text = format->tok->str.bytes;
    state.len = parse_len;
    state.next_arg = spec->first_vararg ? (u32)spec->first_vararg - 1 : 0;
    if (call->nargs) {
        state.used = arena_alloc(s->arena, call->nargs * sizeof(bool), 1);
        state.seen = arena_alloc(s->arena, call->nargs * sizeof(Expected),
                                 _Alignof(Expected));
        state.has_seen = arena_alloc(s->arena, call->nargs * sizeof(bool), 1);
        memset(state.used, 0, call->nargs * sizeof(bool));
        memset(state.has_seen, 0, call->nargs * sizeof(bool));
    }
    switch (spec->family) {
    case FMT_PRINTF:
        check_printf(&state);
        break;
    case FMT_SCANF:
        check_scanf(&state);
        break;
    case FMT_STRFTIME:
        check_strftime(&state);
        break;
    case FMT_STRFMON:
        check_strfmon(&state);
        break;
    }
}

static bool same_literal(const AstNode *a, const AstNode *b)
{
    return a->tok->str.nbytes == b->tok->str.nbytes &&
           (a->tok->str.nbytes == 0 ||
            memcmp(a->tok->str.bytes, b->tok->str.bytes, a->tok->str.nbytes) ==
                0);
}

static void check_literal_alternatives(WarnCtx *w, Sema *s, const AstNode *call,
                                       const FmtSpec *spec, const AstNode *arg,
                                       const AstNode **seen, u32 *nseen)
{
    const AstNode *plain = format_plain(arg);
    u32 i;

    if (plain->kind == AST_EXPR_COND) {
        check_literal_alternatives(w, s, call, spec, plain->mid, seen, nseen);
        check_literal_alternatives(w, s, call, spec, plain->rhs, seen, nseen);
        return;
    }
    if (plain->kind == AST_EXPR_GENERIC && plain->mid) {
        check_literal_alternatives(w, s, call, spec, plain->mid, seen, nseen);
        return;
    }
    for (i = 0; i < *nseen; i++)
        if (same_literal(seen[i], plain))
            return;
    seen[(*nseen)++] = plain;
    check_literal_format(w, s, call, spec, plain);
}

void warn_format_check(WarnCtx *w, Sema *s, const AstNode *call,
                       const FmtSpec *spec)
{
    const AstNode **seen;
    u32 format_index;
    u32 nseen = 0;

    if (!w || !s || !call || !spec || spec->fmt_arg == 0)
        return;
    format_index = (u32)spec->fmt_arg - 1;
    if (format_index >= call->nargs)
        return; /* ordinary prototype arity diagnostics already own this */
    if (null_pointer_constant(s, call->args[format_index]))
        warn_at(w, WARN_NONNULL, call->args[format_index]->span,
                "argument %u null where non-null expected",
                (unsigned)spec->fmt_arg);
    if (!all_literal_formats(call->args[format_index])) {
        if (spec->first_vararg != 0) {
            if (call->nargs < spec->first_vararg)
                warn_at(w, WARN_FORMAT_SECURITY, call->args[format_index]->span,
                        "format not a string literal and no format arguments");
            else
                warn_at(w, WARN_FORMAT_NONLITERAL,
                        call->args[format_index]->span,
                        "format not a string literal, argument types not "
                        "checked");
        }
        return;
    }
    seen = arena_alloc(s->arena,
                       literal_format_count(call->args[format_index]) *
                           sizeof(*seen),
                       _Alignof(const AstNode *));
    check_literal_alternatives(w, s, call, spec, call->args[format_index], seen,
                               &nseen);
}

void warn_format_check_call(WarnCtx *w, Sema *s, const AstNode *call)
{
    const AstNode *callee;
    const BuiltinFmt *row;

    if (!w || !s || !call || call->kind != AST_EXPR_CALL)
        return;
    callee = strip_wrappers(call->lhs, false);
    if (!callee || callee->kind != AST_EXPR_IDENT || !callee->sym)
        return;
    row = builtin_row(s->target, callee->sym->name);
    if (!rough_signature_matches(row, callee->sym))
        return;
    warn_format_check(w, s, call, &row->spec);
}
