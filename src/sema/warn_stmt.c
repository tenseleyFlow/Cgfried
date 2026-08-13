#include "sema/warn_stmt.h"

#include <ctype.h>
#include <string.h>

#include "pp/pp.h"
#include "sema/warn_expr.h"
#include "warn/warn.h"

typedef struct LabelUse LabelUse;
struct LabelUse {
    const char *name;
    Span span;
    bool defined;
    bool used;
    LabelUse *next;
};

typedef struct CaseEntry CaseEntry;
typedef unsigned StmtFlow;

enum {
    FLOW_NONE = 0,
    FLOW_NEXT = 1u << 0,
    FLOW_BREAK = 1u << 1,
    FLOW_CONTINUE = 1u << 2,
};

struct CaseEntry {
    AstNode *node;
    AstNode *first;
    AstNode *last;
    Span comment_target;
    StmtFlow flow;
    bool substantive;
    CaseEntry *prev;
    CaseEntry *next;
};

static LabelUse *label_find(LabelUse *labels, const char *name)
{
    for (; labels; labels = labels->next)
        if (labels->name == name)
            return labels;
    return NULL;
}

static LabelUse *label_get(Sema *s, LabelUse **labels, const char *name)
{
    LabelUse *label = label_find(*labels, name);

    if (label || !name)
        return label;
    label = arena_alloc(s->arena, sizeof(*label), _Alignof(LabelUse));
    memset(label, 0, sizeof(*label));
    label->name = name;
    label->next = *labels;
    *labels = label;
    return label;
}

static void collect_labels(Sema *s, AstNode *st, LabelUse **labels)
{
    u32 i;

    if (!st || st->poisoned)
        return;
    switch (st->kind) {
    case AST_STMT_COMPOUND:
        for (i = 0; i < st->nitems; i++)
            collect_labels(s, st->items[i], labels);
        return;
    case AST_STMT_IF:
        collect_labels(s, st->body, labels);
        collect_labels(s, st->rhs, labels);
        return;
    case AST_STMT_SWITCH:
    case AST_STMT_WHILE:
    case AST_STMT_DO:
    case AST_STMT_FOR:
    case AST_STMT_CASE:
    case AST_STMT_DEFAULT:
        collect_labels(s, st->body, labels);
        return;
    case AST_STMT_LABEL: {
        LabelUse *label = label_get(s, labels, st->name);

        if (label) {
            label->defined = true;
            label->span = st->span;
        }
        collect_labels(s, st->body, labels);
        return;
    }
    case AST_STMT_GOTO: {
        LabelUse *label = label_get(s, labels, st->name);

        if (label)
            label->used = true;
        return;
    }
    default:
        return;
    }
}

static void warn_unused_labels(Sema *s, AstNode *body)
{
    LabelUse *labels = NULL;
    LabelUse *label;

    collect_labels(s, body, &labels);
    for (label = labels; label; label = label->next)
        if (label->defined && !label->used)
            warn_at_ex(s->lang->warnings, WARN_UNUSED_LABEL, label->span,
                       WARN_SUPPRESS_IN_MACRO,
                       "label '%s' defined but not used", label->name);
}

static bool contains_switch_label(AstNode *st)
{
    u32 i;

    if (!st)
        return false;
    if (st->kind == AST_STMT_CASE || st->kind == AST_STMT_DEFAULT)
        return true;
    if (st->kind == AST_STMT_SWITCH)
        return false; /* belongs to the nested switch */
    switch (st->kind) {
    case AST_STMT_COMPOUND:
        for (i = 0; i < st->nitems; i++)
            if (contains_switch_label(st->items[i]))
                return true;
        return false;
    case AST_STMT_IF:
        return contains_switch_label(st->body) ||
               contains_switch_label(st->rhs);
    case AST_STMT_WHILE:
    case AST_STMT_DO:
    case AST_STMT_FOR:
    case AST_STMT_LABEL:
        return contains_switch_label(st->body);
    default:
        return false;
    }
}

static StmtFlow stmt_flow(Sema *s, AstNode *st);

static StmtFlow flow_sequence(StmtFlow before, StmtFlow after)
{
    return (before & ~FLOW_NEXT) | ((before & FLOW_NEXT) ? after : FLOW_NONE);
}

static void case_append(Sema *s, CaseEntry **head, CaseEntry **tail,
                        CaseEntry **current, AstNode *node, Span comment_target)
{
    CaseEntry *entry =
        arena_alloc(s->arena, sizeof(*entry), _Alignof(CaseEntry));

    memset(entry, 0, sizeof(*entry));
    entry->node = node;
    entry->flow = FLOW_NEXT;
    entry->comment_target =
        comment_target.file_id ? comment_target : node->span;
    if (*tail) {
        (*tail)->next = entry;
        entry->prev = *tail;
    } else {
        *head = entry;
    }
    *tail = entry;
    *current = entry;
}

static void scan_switch_segments(Sema *s, AstNode *st, CaseEntry **head,
                                 CaseEntry **tail, CaseEntry **current,
                                 Span label_prefix)
{
    u32 i;

    if (!st)
        return;
    if (st->kind == AST_STMT_CASE || st->kind == AST_STMT_DEFAULT) {
        case_append(s, head, tail, current, st, label_prefix);
        scan_switch_segments(s, st->body, head, tail, current, (Span){0});
        return;
    }
    if (!contains_switch_label(st)) {
        if (*current && st->kind != AST_STMT_NULL) {
            if (!(*current)->first)
                (*current)->first = st;
            (*current)->last = st;
            (*current)->flow =
                flow_sequence((*current)->flow, stmt_flow(s, st));
            (*current)->substantive = true;
        }
        return;
    }
    switch (st->kind) {
    case AST_STMT_COMPOUND:
        for (i = 0; i < st->nitems; i++)
            scan_switch_segments(s, st->items[i], head, tail, current,
                                 (Span){0});
        return;
    case AST_STMT_IF:
        scan_switch_segments(s, st->body, head, tail, current, (Span){0});
        scan_switch_segments(s, st->rhs, head, tail, current, (Span){0});
        return;
    case AST_STMT_WHILE:
    case AST_STMT_DO:
    case AST_STMT_FOR:
        scan_switch_segments(s, st->body, head, tail, current, (Span){0});
        return;
    case AST_STMT_LABEL:
        scan_switch_segments(s, st->body, head, tail, current, st->span);
        return;
    default:
        return;
    }
}

static AstNode *unwrap_expr(AstNode *e)
{
    while (e && ((e->kind == AST_EXPR_CAST && e->implicit) ||
                 e->kind == AST_EXPR_PAREN))
        e = e->lhs;
    return e;
}

static bool stmt_is_noreturn_call(AstNode *st)
{
    AstNode *e;
    AstNode *callee;

    if (!st || st->kind != AST_STMT_EXPR)
        return false;
    e = unwrap_expr(st->lhs);
    if (!e || e->kind != AST_EXPR_CALL)
        return false;
    callee = unwrap_expr(e->lhs);
    return callee && callee->kind == AST_EXPR_IDENT && callee->sym &&
           (callee->sym->func_specs & AST_FS_NORETURN);
}

typedef enum CondTruth {
    COND_FALSE,
    COND_TRUE,
    COND_UNKNOWN,
} CondTruth;

static CondTruth condition_truth(Sema *s, AstNode *condition)
{
    ConstValue cv;

    if (!condition)
        return COND_UNKNOWN;
    cv = constexpr_eval(s, condition, CE_FOLD);
    if (cv.kind != CV_INT)
        return COND_UNKNOWN;
    return cv.i == 0 ? COND_FALSE : COND_TRUE;
}

/* The segment representation is exact when the switch's labels form a
 * linear case chain.  Labels embedded in an if/loop (Duff's device and
 * related forms) need a real CFG; keep those conservatively completing. */
static bool switch_segments_linear(AstNode *st)
{
    u32 i;

    if (!st || st->kind == AST_STMT_SWITCH)
        return true;
    switch (st->kind) {
    case AST_STMT_COMPOUND:
        for (i = 0; i < st->nitems; i++)
            if (!switch_segments_linear(st->items[i]))
                return false;
        return true;
    case AST_STMT_CASE:
    case AST_STMT_DEFAULT:
    case AST_STMT_LABEL:
        return switch_segments_linear(st->body);
    default:
        return !contains_switch_label(st);
    }
}

static StmtFlow switch_flow(Sema *s, AstNode *st)
{
    CaseEntry *head = NULL, *tail = NULL, *current = NULL, *entry;
    StmtFlow next = FLOW_NEXT, all = FLOW_NONE;
    bool has_default = false;

    if (!switch_segments_linear(st->body))
        return FLOW_NEXT;
    scan_switch_segments(s, st->body, &head, &tail, &current, (Span){0});
    for (entry = tail; entry; entry = entry->prev) {
        StmtFlow path = flow_sequence(entry->flow, next);

        if (entry->node && entry->node->kind == AST_STMT_DEFAULT)
            has_default = true;
        if (path & FLOW_BREAK)
            path = (path & ~FLOW_BREAK) | FLOW_NEXT;
        all |= path;
        next = path;
    }
    /* Every case label is a possible jump target.  Without default, an
     * unmatched controlling value also completes the switch immediately. */
    return all | (has_default ? FLOW_NONE : FLOW_NEXT);
}

static StmtFlow loop_flow(Sema *s, AstNode *st)
{
    AstNode *condition = st->kind == AST_STMT_FOR ? st->mid : st->lhs;
    CondTruth truth = st->kind == AST_STMT_FOR && !condition
                          ? COND_TRUE
                          : condition_truth(s, condition);
    StmtFlow body = stmt_flow(s, st->body);
    StmtFlow out = (body & FLOW_BREAK) ? FLOW_NEXT : FLOW_NONE;
    bool reaches_test = (body & (FLOW_NEXT | FLOW_CONTINUE)) != 0;

    /* while/for may execute zero times.  A do body is mandatory, so a body
     * with no path to its condition (return/goto/noreturn) cannot complete
     * even when the condition is nonconstant. */
    if (st->kind != AST_STMT_DO && truth != COND_TRUE)
        out |= FLOW_NEXT;
    if (reaches_test && truth != COND_TRUE)
        out |= FLOW_NEXT;
    return out;
}

static StmtFlow stmt_flow(Sema *s, AstNode *st)
{
    StmtFlow flow;
    CondTruth truth;
    u32 i;

    if (!st || st->poisoned)
        return FLOW_NEXT;
    switch (st->kind) {
    case AST_STMT_COMPOUND:
        flow = FLOW_NEXT;
        for (i = 0; i < st->nitems; i++) {
            StmtFlow item = stmt_flow(s, st->items[i]);
            bool is_label =
                st->items[i] && (st->items[i]->kind == AST_STMT_LABEL ||
                                 st->items[i]->kind == AST_STMT_CASE ||
                                 st->items[i]->kind == AST_STMT_DEFAULT);

            flow = flow_sequence(flow, item);
            if (is_label)
                flow |= item; /* a jump may enter after prior termination */
        }
        return flow;
    case AST_STMT_IF:
        truth = condition_truth(s, st->lhs);
        if (truth == COND_TRUE)
            return stmt_flow(s, st->body);
        if (truth == COND_FALSE)
            return st->rhs ? stmt_flow(s, st->rhs) : FLOW_NEXT;
        return stmt_flow(s, st->body) |
               (st->rhs ? stmt_flow(s, st->rhs) : FLOW_NEXT);
    case AST_STMT_SWITCH:
        return switch_flow(s, st);
    case AST_STMT_WHILE:
    case AST_STMT_DO:
    case AST_STMT_FOR:
        return loop_flow(s, st);
    case AST_STMT_BREAK:
        return FLOW_BREAK;
    case AST_STMT_CONTINUE:
        return FLOW_CONTINUE;
    case AST_STMT_RETURN:
    case AST_STMT_GOTO:
        return FLOW_NONE;
    case AST_STMT_LABEL:
    case AST_STMT_CASE:
    case AST_STMT_DEFAULT:
        return stmt_flow(s, st->body);
    default:
        return stmt_is_noreturn_call(st) ? FLOW_NONE : FLOW_NEXT;
    }
}

static bool stmt_completes(Sema *s, AstNode *st)
{
    return (stmt_flow(s, st) & FLOW_NEXT) != 0;
}

static bool bytes_equal(const char *a, size_t an, const char *b)
{
    size_t bn = strlen(b);

    return an == bn && memcmp(a, b, an) == 0;
}

static bool fallthrough_words_ci(const char *s, size_t n)
{
    size_t i;

    /* Level 2 is intentionally broad: find `fall`/`falls`, an optional
     * horizontal separator, and `through`/`thru` anywhere in the body. */
    for (i = 0; i + 4 <= n; i++) {
        size_t p;

        if (tolower((unsigned char)s[i]) != 'f' ||
            tolower((unsigned char)s[i + 1]) != 'a' ||
            tolower((unsigned char)s[i + 2]) != 'l' ||
            tolower((unsigned char)s[i + 3]) != 'l')
            continue;
        p = i + 4;
        if (p < n && tolower((unsigned char)s[p]) == 's')
            p++;
        while (p < n && (s[p] == ' ' || s[p] == '\t' || s[p] == '-'))
            p++;
        if (p + 4 <= n && tolower((unsigned char)s[p]) == 't' &&
            tolower((unsigned char)s[p + 1]) == 'h' &&
            tolower((unsigned char)s[p + 2]) == 'r' &&
            tolower((unsigned char)s[p + 3]) == 'u')
            return true;
        if (p + 7 <= n && tolower((unsigned char)s[p]) == 't' &&
            tolower((unsigned char)s[p + 1]) == 'h' &&
            tolower((unsigned char)s[p + 2]) == 'r' &&
            tolower((unsigned char)s[p + 3]) == 'o' &&
            tolower((unsigned char)s[p + 4]) == 'u' &&
            tolower((unsigned char)s[p + 5]) == 'g' &&
            tolower((unsigned char)s[p + 6]) == 'h')
            return true;
    }
    return false;
}

static bool take_text(const char *s, size_t n, size_t *at, const char *text)
{
    size_t len = strlen(text);

    if (*at > n || len > n - *at || memcmp(s + *at, text, len) != 0)
        return false;
    *at += len;
    return true;
}

/* GCC 8's level-3 matcher is anchored and case-family aware.  Keeping this
 * as a bounded parser makes the accepted language visible without relying
 * on a host regex implementation. */
static bool fallthrough_level3(const char *s, size_t n)
{
    size_t at = 0;
    bool all_upper = false;
    char f;

    while (at < n &&
           (s[at] == ' ' || s[at] == '\t' || s[at] == '.' || s[at] == '!'))
        at++;
    if (at >= n)
        return false;
    f = s[at];
    if (f == 'E' || f == 'e') {
        if (f == 'E' && at + 4 <= n && memcmp(s + at + 1, "LSE", 3) == 0) {
            all_upper = true;
            at += 4;
        } else if (at + 4 <= n && memcmp(s + at + 1, "lse", 3) == 0) {
            at += 4;
        } else {
            return false;
        }
        if (at < n && s[at] == ',')
            at++;
        if (at >= n || s[at++] != ' ')
            return false;
        if (at >= n || (all_upper && s[at] == 'f') ||
            (f == 'e' && s[at] == 'F'))
            return false;
        f = s[at];
    } else if (f == 'I' || f == 'i') {
        if (f == 'I' && at + 11 <= n &&
            memcmp(s + at + 1, "NTENTIONAL", 10) == 0) {
            all_upper = true;
            at += 11;
        } else if (at + 11 <= n && memcmp(s + at + 1, "ntentional", 10) == 0) {
            at += 11;
        } else {
            return false;
        }
        if (at < n && s[at] == ' ') {
            at++;
            if (at >= n || (all_upper && s[at] == 'f'))
                return false;
        } else if (all_upper) {
            if (!take_text(s, n, &at, "LY "))
                return false;
        } else if (!take_text(s, n, &at, "ly ")) {
            return false;
        }
        if (at >= n || (f == 'i' && s[at] == 'F'))
            return false;
        f = s[at];
    }
    if (f != 'F' && f != 'f')
        return false;
    if (f == 'F' && at + 4 <= n && memcmp(s + at + 1, "ALL", 3) == 0) {
        all_upper = true;
        at += 4;
    } else if (!all_upper && at + 4 <= n && memcmp(s + at + 1, "all", 3) == 0) {
        at += 4;
    } else {
        return false;
    }
    if (at < n && s[at] == (all_upper ? 'S' : 's') && at + 1 < n &&
        s[at + 1] == ' ')
        at += 2;
    else if (at < n && (s[at] == ' ' || s[at] == '-'))
        at++;
    else if (at >= n || s[at] != (all_upper ? 'T' : 't'))
        return false;
    if (at >= n || ((f == 'f' || s[at] != 'T') && (all_upper || s[at] != 't')))
        return false;
    if (all_upper) {
        if (at + 4 <= n && memcmp(s + at, "THRU", 4) == 0)
            at += 4;
        else if (at + 7 <= n && memcmp(s + at, "THROUGH", 7) == 0)
            at += 7;
        else
            return false;
    } else {
        if (at + 4 <= n && (s[at] == 'T' || s[at] == 't') &&
            memcmp(s + at + 1, "hru", 3) == 0)
            at += 4;
        else if (at + 7 <= n && (s[at] == 'T' || s[at] == 't') &&
                 memcmp(s + at + 1, "hrough", 6) == 0)
            at += 7;
        else
            return false;
    }
    while (at < n &&
           (s[at] == ' ' || s[at] == '\t' || s[at] == '.' || s[at] == '!'))
        at++;
    if (at < n && s[at] == '-') {
        at++;
        while (at < n && s[at] != '\n' && s[at] != '\r')
            at++;
    }
    return at == n;
}

static bool trailing_horizontal_space(const char *s, size_t n, size_t at)
{
    while (at < n && (s[at] == ' ' || s[at] == '\t'))
        at++;
    return at == n;
}

static bool fallthrough_comment_matches(const PpComment *comment,
                                        unsigned level)
{
    const char *s;
    size_t n;

    if (!comment || level == 0 || level == 5)
        return false;
    if (level == 1)
        return true;
    s = comment->body;
    n = comment->body_len;
    if (level == 2)
        return fallthrough_words_ci(s, n);
    if (bytes_equal(s, n, "-fallthrough") || bytes_equal(s, n, "@fallthrough@"))
        return true;
    if (n >= 17 && memcmp(s, "lint -fallthrough", 17) == 0 &&
        trailing_horizontal_space(s, n, 17))
        return true;
    if (level == 4) {
        size_t at = 0;

        while (at < n && (s[at] == ' ' || s[at] == '\t'))
            at++;
        if (at + 8 <= n && memcmp(s + at, "FALLTHRU", 8) == 0 &&
            trailing_horizontal_space(s, n, at + 8))
            return true;
        return at + 11 <= n && memcmp(s + at, "FALLTHROUGH", 11) == 0 &&
               trailing_horizontal_space(s, n, at + 11);
    }
    return fallthrough_level3(s, n);
}

static bool successor_immediately_terminates(const CaseEntry *entry)
{
    AstNode *first;

    while (entry && !entry->substantive)
        entry = entry->next;
    first = entry ? entry->first : NULL;
    return first &&
           (first->kind == AST_STMT_BREAK || first->kind == AST_STMT_CONTINUE ||
            first->kind == AST_STMT_GOTO ||
            (first->kind == AST_STMT_RETURN && first->lhs == NULL));
}

/* gcc points at the INNERMOST statement that actually completes normally,
 * not at the outer one that contains it. musl's wordexp.c is
 *
 *     if (a) { ...; break; } else if (b) break;
 *   case '`':
 *
 * where the outer `if` starts on line 75 and the `else if` that really falls
 * through is on line 79 -- gcc says 79 and we said 75. Same warning, wrong
 * finger. Walking to the arm that can complete normally fixes it, and the
 * result is a better diagnostic quite apart from the parity. */
static AstNode *fallthrough_anchor(Sema *s, AstNode *st)
{
    if (!st)
        return NULL;
    switch (st->kind) {
    case AST_STMT_COMPOUND:
        if (st->nitems) {
            AstNode *inner = fallthrough_anchor(s, st->items[st->nitems - 1]);

            if (inner)
                return inner;
        }
        return st;
    case AST_STMT_IF:
        /* Descend only when the then arm terminates and the else arm is the
         * unique route that can reach the next case.  When both arms can
         * complete normally, GCC points at the controlling `if`; choosing
         * the final `else` instead invents a location divergence. */
        if (st->rhs && !stmt_completes(s, st->body) &&
            stmt_completes(s, st->rhs)) {
            AstNode *inner = fallthrough_anchor(s, st->rhs);

            if (inner)
                return inner;
        }
        return st;
    case AST_STMT_LABEL:
        return fallthrough_anchor(s, st->body);
    default:
        return st;
    }
}

/* A CASE LABEL INSIDE A STATICALLY-DEAD BRANCH IS NEVER FALLEN INTO. musl's
 * iconv.c uses the idiom directly:
 *
 *     c = *(wchar_t *)*in;
 *     if (0) {
 *   case UTF_32BE:
 *
 * Control that reaches `if (0)` skips the body entirely, so the label is
 * reachable only by the switch's own jump and there is no fallthrough --
 * gcc says nothing, and it is right.
 *
 * THE TEST IS ON THE LABEL'S ANCESTORS, not on the statement before it. The
 * segment scanner has already descended into the `if` by the time the label
 * is found, so the preceding statement is `c = *(wchar_t *)*in;` and the
 * `if (0)` is nowhere in hand -- a first version checked that and never
 * fired. Walking down from the switch body tracking deadness is what
 * actually answers the question, and it keeps `if (0) { } case X:` -- where
 * the label is a SIBLING and really is fallen into -- warning. */
static bool label_in_dead_arm(Sema *s, AstNode *st, const AstNode *label,
                              bool dead)
{
    u32 i;

    if (!st || !label)
        return false;
    if (st == label)
        return dead;
    if (st->kind == AST_STMT_IF && st->lhs) {
        ConstValue cv = constexpr_eval(s, st->lhs, CE_FOLD);
        bool body_dead = dead, else_dead = dead;

        if (cv.kind == CV_INT) {
            if (cv.i == 0)
                body_dead = true;
            else
                else_dead = true;
        }
        return label_in_dead_arm(s, st->body, label, body_dead) ||
               label_in_dead_arm(s, st->rhs, label, else_dead);
    }
    /* A nested switch owns its own labels; do not walk into one. */
    if (st->kind == AST_STMT_SWITCH)
        return false;
    if (label_in_dead_arm(s, st->body, label, dead) ||
        label_in_dead_arm(s, st->rhs, label, dead))
        return true;
    for (i = 0; i < st->nitems; i++)
        if (label_in_dead_arm(s, st->items[i], label, dead))
            return true;
    return false;
}

static void warn_switch_fallthrough(Sema *s, Preprocessor *pp, AstNode *sw)
{
    CaseEntry *head = NULL, *tail = NULL, *current = NULL, *entry;
    unsigned level = warn_implicit_fallthrough_level(s->lang->warnings);

    if (!level || !sw || sw->kind != AST_STMT_SWITCH)
        return;
    scan_switch_segments(s, sw->body, &head, &tail, &current, (Span){0});
    for (entry = head; entry && entry->next; entry = entry->next) {
        size_t comment_index;
        bool comment_matches = false;

        if (!entry->substantive || !entry->last || !(entry->flow & FLOW_NEXT) ||
            label_in_dead_arm(s, sw->body, entry->next->node, false) ||
            successor_immediately_terminates(entry->next))
            continue;
        for (comment_index = 0; pp; comment_index++) {
            const PpComment *comment = pp_comment_before_n(
                pp, entry->next->comment_target, comment_index);

            if (!comment)
                break;
            if (fallthrough_comment_matches(comment, level)) {
                comment_matches = true;
                break;
            }
        }
        if (comment_matches)
            continue;
        {
            AstNode *at = fallthrough_anchor(s, entry->last);

            warn_at(s->lang->warnings, WARN_IMPLICIT_FALLTHROUGH,
                    (at ? at : entry->last)->span,
                    "this statement may fall through");
        }
    }
}

static bool switch_case_value(Sema *s, const CaseEntry *entry, i64 *out)
{
    ConstValue cv;

    if (!entry || !entry->node || entry->node->kind != AST_STMT_CASE ||
        !entry->node->lhs)
        return false;
    cv = constexpr_eval(s, entry->node->lhs, CE_FOLD);
    if (cv.kind != CV_INT)
        return false;
    *out = (i64)cv.i;
    return true;
}

static bool switch_has_enum_value(Sema *s, const CaseEntry *head, i64 value)
{
    const CaseEntry *entry;

    for (entry = head; entry; entry = entry->next) {
        i64 cv;

        if (switch_case_value(s, entry, &cv) && cv == value)
            return true;
    }
    return false;
}

static bool enum_contains_value(const AstNode *enum_ast, i64 value)
{
    u32 i;

    if (!enum_ast)
        return false;
    for (i = 0; i < enum_ast->nmembers; i++) {
        const AstNode *item = enum_ast->members[i];

        if (item && item->sym && item->sym->enum_value == value)
            return true;
    }
    return false;
}

static void warn_switch_coverage(Sema *s, AstNode *sw)
{
    CaseEntry *head = NULL, *tail = NULL, *current = NULL, *entry;
    AstNode *control_expr;
    Type *control;
    AstNode *enum_ast;
    bool has_default = false;
    bool use_switch_enum;
    WarnId missing_id;
    u32 i;

    if (!sw || sw->kind != AST_STMT_SWITCH)
        return;
    scan_switch_segments(s, sw->body, &head, &tail, &current, (Span){0});
    for (entry = head; entry; entry = entry->next)
        if (entry->node && entry->node->kind == AST_STMT_DEFAULT)
            has_default = true;
    if (!has_default)
        warn_at(s->lang->warnings, WARN_SWITCH_DEFAULT, sw->span,
                "switch missing default case");

    /* Sema materializes the integer promotions required by 6.8.4.2p5 on
     * the controlling expression.  Coverage diagnostics still describe the
     * source enum, so peel only those implicit conversion nodes before
     * consulting its enumerator table. */
    control_expr = sw->lhs;
    while (control_expr && control_expr->kind == AST_EXPR_CAST &&
           control_expr->implicit && control_expr->lhs)
        control_expr = control_expr->lhs;
    control = control_expr ? control_expr->sem_type : NULL;
    if (!control || control->kind != TY_ENUM || !control->tag ||
        !control->tag->enum_ast)
        return;
    enum_ast = control->tag->enum_ast;
    use_switch_enum =
        warn_enabled(s->lang->warnings, WARN_SWITCH_ENUM, sw->span);
    missing_id = use_switch_enum ? WARN_SWITCH_ENUM : WARN_SWITCH;
    if (use_switch_enum || !has_default) {
        for (i = 0; i < enum_ast->nmembers; i++) {
            AstNode *item = enum_ast->members[i];

            if (!item || !item->sym ||
                switch_has_enum_value(s, head, item->sym->enum_value))
                continue;
            warn_at(s->lang->warnings, missing_id, sw->span,
                    "enumeration value '%s' not handled in switch", item->name);
        }
    }
    for (entry = head; entry; entry = entry->next) {
        i64 cv;

        if (switch_case_value(s, entry, &cv) &&
            !enum_contains_value(enum_ast, cv))
            warn_at(s->lang->warnings, missing_id, entry->node->span,
                    "case value '%lld' not in enumerated type", (long long)cv);
    }
}

static SourceFile *source_for_span(Preprocessor *pp, Span sp)
{
    size_t i;

    if (!pp || !sp.file_id)
        return NULL;
    for (i = 0; i < pp->nfiles; i++)
        if (pp->files[i] && pp->files[i]->diag_file_id == sp.file_id)
            return pp->files[i];
    return NULL;
}

typedef struct IndentInfo {
    u32 column;
    bool tabs;
    bool spaces;
    bool valid;
} IndentInfo;

static IndentInfo source_indent(Preprocessor *pp, Span sp)
{
    IndentInfo out = {0};
    SourceFile *sf = source_for_span(pp, sp);
    const char *p;
    u32 col = 1;

    if (!sf || sp.line == 0 || sp.line > sf->nlines)
        return out;
    p = sf->contents + sf->line_offsets[sp.line - 1];
    while (*p == ' ' || *p == '\t') {
        if (*p == ' ') {
            out.spaces = true;
            col++;
        } else {
            out.tabs = true;
            col += 8 - ((col - 1) & 7u);
        }
        p++;
    }
    out.column = col;
    out.valid = true;
    return out;
}

static bool indentation_compatible(IndentInfo a, IndentInfo b, IndentInfo c)
{
    bool any_tabs = a.tabs || b.tabs || c.tabs;
    bool any_spaces = a.spaces || b.spaces || c.spaces;

    /* GCC deliberately abandons this heuristic when indentation style is
     * ambiguous.  Bail both for a mixed prefix and for a three-line sample
     * that changes indentation alphabet. */
    if ((a.tabs && a.spaces) || (b.tabs && b.spaces) || (c.tabs && c.spaces))
        return false;
    return !(any_tabs && any_spaces);
}

static AstNode *unbraced_control_body(AstNode *st)
{
    if (!st)
        return NULL;
    switch (st->kind) {
    case AST_STMT_IF:
        /* An else completes the visual chain.  GCC does not claim that a
         * following sibling was guarded by the if's first arm. */
        if (st->rhs)
            return NULL;
        break;
    case AST_STMT_WHILE:
    case AST_STMT_FOR:
        break;
    default:
        return NULL;
    }
    if (!st->body || st->body->kind == AST_STMT_COMPOUND)
        return NULL;
    return st->body;
}

static void warn_misleading_pair(Sema *s, Preprocessor *pp, AstNode *control,
                                 AstNode *next)
{
    AstNode *body = unbraced_control_body(control);
    IndentInfo ci, bi, ni;
    bool looks_guarded;

    if (!body || !next || next->kind == AST_STMT_DECL)
        return;
    if ((control->span.origin | body->span.origin | next->span.origin) &
        SPAN_ORIGIN_ANY_MACRO)
        return;
    if (control->span.file_id != body->span.file_id ||
        control->span.file_id != next->span.file_id)
        return;
    ci = source_indent(pp, control->span);
    bi = source_indent(pp, body->span);
    ni = source_indent(pp, next->span);
    if (!ci.valid || !bi.valid || !ni.valid ||
        !indentation_compatible(ci, bi, ni))
        return;

    looks_guarded =
        next->span.line == body->span.line ||
        (body->span.line == control->span.line ? ni.column > ci.column
                                               : ni.column == bi.column);
    if (!looks_guarded || next->span.line <= body->span.line)
        return;
    if (!warn_enabled(s->lang->warnings, WARN_MISLEADING_INDENTATION,
                      control->span))
        return;
    warn_at(s->lang->warnings, WARN_MISLEADING_INDENTATION, control->span,
            "this '%s' clause does not guard the following statement",
            control->kind == AST_STMT_IF
                ? "if"
                : (control->kind == AST_STMT_WHILE ? "while" : "for"));
    diag_emit(s->dc, DIAG_NOTE, next->span,
              "this statement is misleadingly indented as if it were guarded");
}

static void warn_misleading_stmt(Sema *s, Preprocessor *pp, AstNode *st)
{
    u32 i;

    if (!st || st->poisoned)
        return;
    switch (st->kind) {
    case AST_STMT_COMPOUND:
        for (i = 1; i < st->nitems; i++)
            warn_misleading_pair(s, pp, st->items[i - 1], st->items[i]);
        for (i = 0; i < st->nitems; i++)
            warn_misleading_stmt(s, pp, st->items[i]);
        return;
    case AST_STMT_IF:
        warn_misleading_stmt(s, pp, st->body);
        warn_misleading_stmt(s, pp, st->rhs);
        return;
    case AST_STMT_SWITCH:
        warn_switch_fallthrough(s, pp, st);
        warn_switch_coverage(s, st);
        warn_misleading_stmt(s, pp, st->body);
        return;
    case AST_STMT_WHILE:
    case AST_STMT_DO:
    case AST_STMT_FOR:
    case AST_STMT_CASE:
    case AST_STMT_DEFAULT:
    case AST_STMT_LABEL:
        warn_misleading_stmt(s, pp, st->body);
        return;
    default:
        return;
    }
}

static void warn_initializer(Sema *s, Type *destination, AstNode *init)
{
    u32 i;

    if (!init)
        return;
    if (init->kind == AST_INIT_LIST) {
        for (i = 0; i < init->nitems; i++) {
            AstNode *item = init->items[i];

            if (!item)
                continue;
            if (item->kind == AST_INIT_LIST)
                warn_initializer(s, NULL, item);
            else {
                Type *item_type = item->kind == AST_EXPR_CAST && item->implicit
                                      ? item->sem_type
                                      : NULL;

                if (item_type)
                    sema_warn_implicit_conversion(s, item_type, item);
                sema_warn_expr(s, item, SEMA_WARN_EXPR_VALUE);
            }
        }
        return;
    }
    if (destination)
        sema_warn_implicit_conversion(s, destination, init);
    sema_warn_expr(s, init, SEMA_WARN_EXPR_VALUE);
}

static void warn_decl_exprs(Sema *s, AstNode *d)
{
    u32 i;

    if (!d)
        return;
    warn_initializer(s, d->sem_type, d->init);
    if (d->alignas_expr)
        sema_warn_expr(s, d->alignas_expr, SEMA_WARN_EXPR_VALUE);
    for (i = 0; i < d->nitems; i++)
        warn_decl_exprs(s, d->items[i]);
}

static void warn_statement_exprs(Sema *s, AstNode *st, Type *return_type)
{
    u32 i;

    if (!st || st->poisoned)
        return;
    switch (st->kind) {
    case AST_STMT_COMPOUND:
        for (i = 0; i < st->nitems; i++)
            warn_statement_exprs(s, st->items[i], return_type);
        return;
    case AST_STMT_DECL:
        warn_decl_exprs(s, st->lhs);
        return;
    case AST_STMT_EXPR:
        sema_warn_expr(s, st->lhs, SEMA_WARN_EXPR_DISCARDED);
        return;
    case AST_STMT_RETURN:
        if (st->lhs) {
            if (return_type)
                sema_warn_implicit_conversion(s, return_type, st->lhs);
            sema_warn_expr(s, st->lhs, SEMA_WARN_EXPR_VALUE);
        }
        return;
    case AST_STMT_IF:
        sema_warn_expr(s, st->lhs, SEMA_WARN_EXPR_TRUTH);
        warn_statement_exprs(s, st->body, return_type);
        warn_statement_exprs(s, st->rhs, return_type);
        return;
    case AST_STMT_SWITCH:
        sema_warn_expr(s, st->lhs, SEMA_WARN_EXPR_VALUE);
        warn_statement_exprs(s, st->body, return_type);
        return;
    case AST_STMT_WHILE:
    case AST_STMT_DO:
        sema_warn_expr(s, st->lhs, SEMA_WARN_EXPR_TRUTH);
        warn_statement_exprs(s, st->body, return_type);
        return;
    case AST_STMT_FOR:
        if (st->lhs) {
            if (st->lhs->kind == AST_DECL)
                warn_decl_exprs(s, st->lhs);
            else
                warn_statement_exprs(s, st->lhs, return_type);
        }
        sema_warn_expr(s, st->mid, SEMA_WARN_EXPR_TRUTH);
        sema_warn_expr(s, st->rhs, SEMA_WARN_EXPR_DISCARDED);
        warn_statement_exprs(s, st->body, return_type);
        return;
    case AST_STMT_CASE:
        sema_warn_expr(s, st->lhs, SEMA_WARN_EXPR_VALUE);
        warn_statement_exprs(s, st->body, return_type);
        return;
    case AST_STMT_DEFAULT:
    case AST_STMT_LABEL:
        warn_statement_exprs(s, st->body, return_type);
        return;
    default:
        return;
    }
}

void sema_warn_translation_unit(Sema *s, AstNode *tu, Preprocessor *pp)
{
    u32 i;

    if (!s || !tu || tu->kind != AST_TRANSLATION_UNIT)
        return;
    for (i = 0; i < tu->ndecls; i++) {
        AstNode *d = tu->decls[i];

        if (!d)
            continue;
        warn_decl_exprs(s, d);
        if (d->kind != AST_FUNC_DEF || !d->body)
            continue;
        warn_unused_labels(s, d->body);
        warn_misleading_stmt(s, pp, d->body);
        warn_statement_exprs(s, d->body,
                             d->sem_type && d->sem_type->kind == TY_FUNC
                                 ? d->sem_type->base
                                 : NULL);
    }
}
