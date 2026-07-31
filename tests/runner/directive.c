#include "directive.h"

#include <string.h>

#include "target.h"

typedef struct {
    const char *name;
    DirectiveKind kind;
} DirectiveName;

static const DirectiveName directive_table[] = {
    {"CHECK", DIR_CHECK},
    {"EXIT_CODE", DIR_EXIT_CODE},
    {"ERROR_EXPECTED", DIR_ERROR_EXPECTED},
    {"WARNING_EXPECTED", DIR_WARNING_EXPECTED},
    {"XFAIL", DIR_XFAIL},
    {"SKIP", DIR_SKIP},
    {"TIMEOUT", DIR_TIMEOUT},
    {"FLAGS", DIR_FLAGS},
    {"ENV", DIR_ENV},
    {"OPT_EQ", DIR_OPT_EQ},
    {"ASM_CHECK", DIR_ASM_CHECK},
    {"IR_CHECK", DIR_IR_CHECK},
    {"IR_CHECK-NOT", DIR_IR_CHECK_NOT},
    {"MIR_CHECK", DIR_MIR_CHECK},
};

typedef struct {
    Arena *arena;
    DirectiveSet *set;
    size_t dirs_cap;
    size_t errs_cap;
    bool seen_exit_code;
    bool seen_timeout;
    bool seen_flags;
} Parser;

static void err(Parser *p, u32 line, const char *msg)
{
    DirectiveSet *s = p->set;

    if (s->nerrs == p->errs_cap) {
        size_t cap = p->errs_cap ? p->errs_cap * 2 : 8;
        DirectiveError *grown = arena_alloc(
            p->arena, cap * sizeof(DirectiveError), _Alignof(DirectiveError));
        if (s->nerrs) /* memcpy from NULL is UB even for 0 */
            memcpy(grown, s->errs, s->nerrs * sizeof(DirectiveError));
        s->errs = grown;
        p->errs_cap = cap;
    }
    s->errs[s->nerrs].line = line;
    s->errs[s->nerrs].msg = msg;
    s->nerrs++;
}

/* err() with the offending token quoted: "<prefix>'<detail>'". */
static void errf(Parser *p, u32 line, const char *prefix, const char *detail,
                 size_t detail_len)
{
    size_t plen = strlen(prefix);
    char *m = arena_alloc(p->arena, plen + detail_len + 3, 1);

    memcpy(m, prefix, plen);
    m[plen] = '\'';
    memcpy(m + plen + 1, detail, detail_len);
    m[plen + 1 + detail_len] = '\'';
    m[plen + 2 + detail_len] = '\0';
    err(p, line, m);
}

static void add_dir(Parser *p, Directive d)
{
    DirectiveSet *s = p->set;

    if (s->ndirs == p->dirs_cap) {
        size_t cap = p->dirs_cap ? p->dirs_cap * 2 : 8;
        Directive *grown =
            arena_alloc(p->arena, cap * sizeof(Directive), _Alignof(Directive));
        if (s->ndirs) /* memcpy from NULL is UB even for 0 */
            memcpy(grown, s->dirs, s->ndirs * sizeof(Directive));
        s->dirs = grown;
        p->dirs_cap = cap;
    }
    s->dirs[s->ndirs++] = d;
}

static bool is_upper_or_underscore(char c)
{
    /* '-' joined the set with IR_CHECK-NOT (Sprint 18); a hyphenated
     * ALL-CAPS typo now errors loudly instead of matching nothing. */
    return (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
}

static bool selector_valid(const char *sel)
{
    int i;

    if (strcmp(sel, "*") == 0)
        return true;
    for (i = 0; i < CGF_TARGET_COUNT; i++)
        if (strcmp(sel, cgf_target_names[i]) == 0)
            return true;
    return false;
}

bool directive_selector_matches(const char *selector, const char *target)
{
    return strcmp(selector, "*") == 0 || strcmp(selector, target) == 0;
}

/* Parses a nonnegative int (at most 4 digits — enough for exit codes,
 * timeouts, and XF-NNNN ids; range checks are the caller's). -1 = malformed. */
static int parse_int(const char *s, size_t len)
{
    int v = 0;
    size_t i;

    if (len == 0 || len > 4)
        return -1;
    for (i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

/* One line, already stripped of its trailing newline (and \r for CRLF
 * sources). `line_no` is 1-based. */
static void parse_line(Parser *p, const char *line, size_t len, u32 line_no)
{
    size_t i = 0, name_start, name_len, sel_start = 0, sel_len = 0;
    size_t spaces;
    bool at_line_start;
    const char *slash;
    const char *name;
    const DirectiveName *hit = NULL;
    size_t t;

    /* Find the first "//". Directive-shaped comments after code are rejected
     * (heuristic: requires "// " — a bare "://" as in URLs stays exempt). */
    slash = NULL;
    for (i = 0; i + 1 < len; i++) {
        if (line[i] == '/' && line[i + 1] == '/') {
            slash = line + i;
            break;
        }
    }
    if (!slash)
        return;
    {
        size_t before = (size_t)(slash - line);
        size_t j;

        at_line_start = true;
        for (j = 0; j < before; j++) {
            if (line[j] != ' ' && line[j] != '\t') {
                at_line_start = false;
                break;
            }
        }
    }

    i = (size_t)(slash - line) + 2;
    spaces = 0;
    while (i < len && line[i] == ' ') {
        spaces++;
        i++;
    }
    name_start = i;
    while (i < len && is_upper_or_underscore(line[i]))
        i++;
    name_len = i - name_start;
    if (name_len == 0)
        return; /* not directive-shaped */

    /* Optional (selector). */
    if (i < len && line[i] == '(') {
        size_t close = i + 1;
        while (close < len && line[close] != ')')
            close++;
        if (close == len)
            return; /* unterminated paren: not directive-shaped */
        sel_start = i + 1;
        sel_len = close - sel_start;
        i = close + 1;
    }
    if (i >= len || line[i] != ':')
        return; /* no colon: a plain comment (NOTE, TODO without colon...) */

    /* From here on this IS directive-shaped; all deviations are errors. */
    if (!at_line_start) {
        err(p, line_no,
            "directive must start its line (only whitespace may "
            "precede '//')");
        return;
    }
    if (spaces != 1) {
        err(p, line_no,
            "directive spacing must be exactly '// NAME:' (one space after "
            "'//')");
        return;
    }

    name = line + name_start;
    for (t = 0; t < CGF_ARRAY_LEN(directive_table); t++) {
        if (strlen(directive_table[t].name) == name_len &&
            memcmp(directive_table[t].name, name, name_len) == 0) {
            hit = &directive_table[t];
            break;
        }
    }
    if (!hit) {
        errf(p, line_no, "unknown directive ", name, name_len);
        return;
    }

    i++; /* past ':' */
    if (i < len && line[i] == ' ')
        i++;
    else if (i < len) {
        err(p, line_no,
            "directive value must be separated by one space "
            "(': value')");
        return;
    }

    {
        const char *value = line + i;
        size_t value_len = len - i;
        Directive d;

        memset(&d, 0, sizeof(d));
        d.kind = hit->kind;
        d.line = line_no;

        /* Reserved directives: parse, then hard-error naming their sprint. */
        if (hit->kind == DIR_OPT_EQ) {
            err(p, line_no, "OPT_EQ is not yet supported: Sprint 30");
            return;
        }
        if (hit->kind == DIR_ASM_CHECK) {
            err(p, line_no, "ASM_CHECK is not yet supported: Sprint 24");
            return;
        }
        /* Selector legality per directive. */
        if (hit->kind == DIR_XFAIL || hit->kind == DIR_SKIP) {
            if (sel_len == 0) {
                err(p, line_no,
                    hit->kind == DIR_XFAIL
                        ? "XFAIL requires a target selector: "
                          "XFAIL(<target>|*)"
                        : "SKIP requires a target selector: "
                          "SKIP(<target>|*)");
                return;
            }
            {
                char *sel = arena_strndup(p->arena, line + sel_start, sel_len);
                if (!selector_valid(sel)) {
                    errf(p, line_no, "unknown target selector ", sel,
                         strlen(sel));
                    return;
                }
                d.selector = sel;
            }
        } else if (sel_len != 0) {
            err(p, line_no,
                "this directive does not take a (selector) yet (target-"
                "qualified CHECKs land in Sprint 49)");
            return;
        }

        switch (hit->kind) {
        case DIR_CHECK:
        case DIR_IR_CHECK:     /* Sprint 17: CHECK semantics against the
                                  -emit-ir reprint of a fixture */
        case DIR_MIR_CHECK:    /* Sprint 21: vs -emit-mir stdout */
        case DIR_IR_CHECK_NOT: /* Sprint 18: the text must NOT appear */
        case DIR_ERROR_EXPECTED:
        case DIR_WARNING_EXPECTED:
            if (value_len == 0) {
                err(p, line_no, "empty directive value");
                return;
            }
            d.value = arena_strndup(p->arena, value, value_len);
            break;
        case DIR_EXIT_CODE: {
            int v = parse_int(value, value_len);
            if (v < 0 || v > 255) {
                err(p, line_no, "EXIT_CODE must be an integer in 0..255");
                return;
            }
            if (p->seen_exit_code) {
                err(p, line_no, "duplicate EXIT_CODE directive");
                return;
            }
            p->seen_exit_code = true;
            p->set->exit_code = v;
            break;
        }
        case DIR_TIMEOUT: {
            int v = parse_int(value, value_len);
            if (v < 1 || v > 600) {
                err(p, line_no,
                    "TIMEOUT must be an integer in 1..600 "
                    "(seconds)");
                return;
            }
            if (p->seen_timeout) {
                err(p, line_no, "duplicate TIMEOUT directive");
                return;
            }
            p->seen_timeout = true;
            p->set->timeout = v;
            break;
        }
        case DIR_XFAIL: {
            /* Value: "XF-NNNN <reason>". */
            size_t idlen = 0;
            while (idlen < value_len && value[idlen] != ' ')
                idlen++;
            if (idlen != 7 || memcmp(value, "XF-", 3) != 0 ||
                parse_int(value + 3, 4) < 0) {
                err(p, line_no,
                    "XFAIL must cite a ledger id: XFAIL(<sel>): XF-NNNN "
                    "<reason> (.docs/audits/xfail-debt.md)");
                return;
            }
            if (idlen + 1 >= value_len) {
                err(p, line_no, "XFAIL needs a reason after the XF-id");
                return;
            }
            d.xf_id = arena_strndup(p->arena, value, idlen);
            d.value = arena_strndup(p->arena, value + idlen + 1,
                                    value_len - idlen - 1);
            break;
        }
        case DIR_SKIP:
            if (value_len == 0) {
                err(p, line_no, "SKIP needs a reason");
                return;
            }
            d.value = arena_strndup(p->arena, value, value_len);
            break;
        case DIR_FLAGS:
            /* Extra compiler argv pieces (space-separated), e.g. -E. */
            if (value_len == 0) {
                err(p, line_no, "FLAGS needs at least one flag");
                return;
            }
            if (p->seen_flags) {
                err(p, line_no, "duplicate FLAGS directive");
                return;
            }
            p->seen_flags = true;
            p->set->flags = arena_strndup(p->arena, value, value_len);
            break;
        case DIR_ENV: {
            /* NAME=VALUE for the compile step; repeatable. */
            size_t eq = 0;
            while (eq < value_len && value[eq] != '=')
                eq++;
            if (eq == 0 || eq == value_len) {
                err(p, line_no, "ENV must be NAME=VALUE");
                return;
            }
            d.value = arena_strndup(p->arena, value, value_len);
            break;
        }
        case DIR_OPT_EQ:
        case DIR_ASM_CHECK:
            /* Unreachable: reserved directives returned above. Exhaustive
             * switch, no default — adding a directive must break here. */
            break;
        }

        if (hit->kind == DIR_ERROR_EXPECTED)
            p->set->has_error_expected = true;
        if (hit->kind == DIR_WARNING_EXPECTED)
            p->set->has_warning_expected = true;
        /* F-S22-MIRCHECK: DIR_MIR_CHECK was missing from this list for
         * all of Sprint 21 — MIR_CHECK directives parsed, validated, and
         * were then silently DROPPED, so the nine MIR goldens asserted
         * nothing. A meta fixture now pins that an unmatched MIR_CHECK
         * fails. */
        if (hit->kind == DIR_CHECK || hit->kind == DIR_IR_CHECK ||
            hit->kind == DIR_MIR_CHECK || hit->kind == DIR_IR_CHECK_NOT ||
            hit->kind == DIR_ERROR_EXPECTED ||
            hit->kind == DIR_WARNING_EXPECTED || hit->kind == DIR_XFAIL ||
            hit->kind == DIR_SKIP || hit->kind == DIR_ENV)
            add_dir(p, d);
    }
}

void directive_parse(Arena *a, const char *src, size_t len, DirectiveSet *out)
{
    Parser p;
    size_t pos = 0;
    u32 line_no = 1;

    memset(out, 0, sizeof(*out));
    memset(&p, 0, sizeof(p));
    p.arena = a;
    p.set = out;

    while (pos < len) {
        size_t eol = pos;
        size_t line_len;

        while (eol < len && src[eol] != '\n')
            eol++;
        line_len = eol - pos;
        /* CRLF: the \r is line terminator, not value content. */
        if (line_len > 0 && src[pos + line_len - 1] == '\r')
            line_len--;
        parse_line(&p, src + pos, line_len, line_no);
        pos = eol + 1;
        line_no++;
    }

    /* Cross-directive constraints. */
    if (out->has_error_expected) {
        size_t i;
        for (i = 0; i < out->ndirs; i++) {
            if (out->dirs[i].kind == DIR_CHECK ||
                out->dirs[i].kind == DIR_IR_CHECK ||
                out->dirs[i].kind == DIR_IR_CHECK_NOT) {
                err(&p, out->dirs[i].line,
                    "CHECK is meaningless with ERROR_EXPECTED (the compile "
                    "step stops before running anything)");
                break;
            }
        }
    }
}
