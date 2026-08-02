#include "warn/warn.h"

#include <stdlib.h>
#include <string.h>

#include "util/arena.h"

typedef signed char WarnSetting;
enum { WS_UNSET = -1, WS_OFF = 0, WS_ON = 1 };

typedef enum EventKind { EV_PUSH, EV_POP, EV_SET } EventKind;

typedef struct WarnEvent {
    u32 seq;
    EventKind kind;
    WarnId id;
    WarnPragmaClass classification;
    const signed char *snapshot; /* EV_POP: state restored by this pop */
} WarnEvent;

struct WarnCtx {
    Arena *arena;
    DiagCtx *diag;
    WarnSetting enabled[WARN_COUNT];
    unsigned char enabled_priority[WARN_COUNT];
    WarnSetting promoted[WARN_COUNT];
    unsigned char promoted_priority[WARN_COUNT];
    WarnSetting group_enabled[8];
    signed char pragma_state[WARN_COUNT]; /* -1 or WarnPragmaClass */
    const signed char **pragma_stack;
    size_t stack_len, stack_cap;
    WarnEvent *events;
    size_t events_len, events_cap;
    unsigned pedantic; /* 0 off, 1 warn, 2 errors */
    bool global_error;
    bool inhibit;
    bool system_headers;
};

static const WarnInfo infos[] = {
    {WARN_NONE, NULL, WG_NONE, WD_OFF, WL_WARN},
#define W(id, flag, groups, defstate, level)                                   \
    {id, flag, groups, defstate, level},
#include "warn/warnings.def"
#undef W
};

static int compare_info_flag(const void *key, const void *item)
{
    const char *flag = key;
    const WarnInfo *info = item;

    return strcmp(flag, info->flag);
}

static const char *strip_w(const char *arg)
{
    if (arg && arg[0] == '-' && arg[1] == 'W')
        return arg + 2;
    return arg;
}

static bool normalized_name(const char *arg, char *out, size_t out_size,
                            bool *negative)
{
    const char *p = strip_w(arg);
    const char *eq;
    size_t n;

    *negative = false;
    if (!p)
        return false;
    if (strncmp(p, "no-", 3) == 0) {
        *negative = true;
        p += 3;
    }
    eq = strchr(p, '=');
    n = eq ? (size_t)(eq - p) : strlen(p);
    if (n == 0 || n >= out_size)
        return false;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

size_t warn_info_count(void)
{
    return WARN_COUNT - 1;
}

const WarnInfo *warn_info_at(size_t index)
{
    return index < warn_info_count() ? &infos[index + 1] : NULL;
}

const WarnInfo *warn_info_for_id(WarnId id)
{
    if (id <= WARN_NONE || id >= WARN_COUNT)
        return NULL;
    return &infos[id];
}

const WarnInfo *warn_info_for_flag(const char *flag)
{
    char name[128];
    bool negative;

    if (!normalized_name(flag, name, sizeof(name), &negative))
        return NULL;
    (void)negative;
    return bsearch(name, infos + 1, WARN_COUNT - 1, sizeof(infos[0]),
                   compare_info_flag);
}

const char *warn_flag_name(WarnId id)
{
    const WarnInfo *info = warn_info_for_id(id);

    return info ? info->flag : NULL;
}

static unsigned group_for_name(const char *name)
{
    static const struct {
        const char *name;
        unsigned group;
    } groups[] = {{"all", WG_ALL},           {"extra", WG_EXTRA},
                  {"unused", WG_UNUSED},     {"format", WG_FORMAT},
                  {"implicit", WG_IMPLICIT}, {"pedantic", WG_PEDANTIC},
                  {"cgf-ext", WG_CGF_EXT}};
    size_t i;

    for (i = 0; i < sizeof(groups) / sizeof(groups[0]); i++)
        if (strcmp(name, groups[i].name) == 0)
            return groups[i].group;
    return WG_NONE;
}

/* Returns the requested GCC format-warning level, -1 for an invalid format
 * option, or -2 when `p` is not a format-level option. GCC accepts levels
 * 0..2 and rejects parameterized negative spellings such as
 * -Wno-format=2. */
static int format_option_level(const char *p)
{
    if (strcmp(p, "format") == 0)
        return 1;
    if (strcmp(p, "no-format") == 0)
        return 0;
    if (strncmp(p, "no-format=", 10) == 0)
        return -1;
    if (strncmp(p, "format=", 7) != 0)
        return -2;
    if (p[7] >= '0' && p[7] <= '2' && p[8] == '\0')
        return p[7] - '0';
    return -1;
}

bool warn_option_known(const char *arg)
{
    char name[128];
    bool negative;
    const char *p;

    if (!arg)
        return false;
    if (strcmp(arg, "-w") == 0 || strcmp(arg, "w") == 0 ||
        strcmp(arg, "-pedantic") == 0 || strcmp(arg, "pedantic") == 0 ||
        strcmp(arg, "-pedantic-errors") == 0 ||
        strcmp(arg, "pedantic-errors") == 0)
        return true;
    p = strip_w(arg);
    {
        int format_level = format_option_level(p);

        if (format_level != -2)
            return format_level >= 0;
    }
    if (strncmp(p, "error=", 6) == 0)
        return group_for_name(p + 6) != WG_NONE ||
               warn_info_for_flag(p + 6) != NULL;
    if (strncmp(p, "no-error=", 9) == 0)
        return group_for_name(p + 9) != WG_NONE ||
               warn_info_for_flag(p + 9) != NULL;
    if (!normalized_name(arg, name, sizeof(name), &negative))
        return false;
    (void)negative;
    if (strcmp(name, "error") == 0 || strcmp(name, "fatal-errors") == 0 ||
        strcmp(name, "system-headers") == 0)
        return true;
    return group_for_name(name) != WG_NONE || warn_info_for_flag(name) != NULL;
}

WarnCtx *warn_ctx_new(Arena *arena, DiagCtx *diag)
{
    WarnCtx *w = arena_alloc(arena, sizeof(*w), _Alignof(WarnCtx));
    size_t i;

    memset(w, 0, sizeof(*w));
    w->arena = arena;
    w->diag = diag;
    for (i = 0; i < WARN_COUNT; i++) {
        w->enabled[i] = WS_UNSET;
        w->promoted[i] = WS_UNSET;
        w->pragma_state[i] = -1;
    }
    for (i = 0; i < sizeof(w->group_enabled) / sizeof(w->group_enabled[0]); i++)
        w->group_enabled[i] = WS_UNSET;
    return w;
}

void warn_print_help(FILE *out)
{
    size_t i;

    fputs("Warning options (-Wall is a selected group, not every warning):\n",
          out);
    for (i = 1; i < WARN_COUNT; i++)
        fprintf(out, "  -W%-43s %s\n", infos[i].flag,
                infos[i].default_state == WD_ON ? "[enabled]" : "[disabled]");
}

DiagCtx *warn_diag(WarnCtx *w)
{
    return w ? w->diag : NULL;
}

static unsigned group_index(unsigned group)
{
    unsigned i = 0;

    while ((1u << i) != group)
        i++;
    return i;
}

static void apply_group(WarnCtx *w, unsigned group, bool on)
{
    size_t i;
    unsigned char priority = (group == WG_ALL || group == WG_EXTRA) ? 1u : 2u;

    w->group_enabled[group_index(group)] = on ? WS_ON : WS_OFF;
    for (i = 1; i < WARN_COUNT; i++) {
        unsigned groups = infos[i].groups;

        if (!(groups & group))
            continue;
        /* GCC C enables these only for the intersection of -Wextra and
         * -Wunused. Encoding both memberships in the row keeps that law
         * out of warning-id special cases. */
        if ((groups & (WG_EXTRA | WG_UNUSED)) == (WG_EXTRA | WG_UNUSED)) {
            /* Their effective state is the conjunction, computed at query
             * time. Keeping group state separate makes -Wno-extra undo the
             * condition even though -Wunused is the more specific group. */
            continue;
        }
        if (w->enabled_priority[i] <= priority) {
            w->enabled[i] = on ? WS_ON : WS_OFF;
            w->enabled_priority[i] = priority;
        }
    }
}

static void apply_format_level(WarnCtx *w, unsigned level)
{
    apply_group(w, WG_FORMAT, level != 0);
    apply_group(w, WG_FORMAT2, level == 2);
}

static void apply_promotion_group(WarnCtx *w, unsigned group, bool demote)
{
    size_t i;
    unsigned char priority = (group == WG_ALL || group == WG_EXTRA) ? 1u : 2u;

    for (i = 1; i < WARN_COUNT; i++) {
        if ((infos[i].groups & group) && w->promoted_priority[i] <= priority) {
            w->promoted[i] = demote ? WS_OFF : WS_ON;
            w->promoted_priority[i] = priority;
        }
    }
}

static bool parse_named_error(WarnCtx *w, const char *p, bool demote)
{
    const WarnInfo *info = warn_info_for_flag(p);
    char name[128];
    bool negative;
    unsigned group;

    if (!normalized_name(p, name, sizeof(name), &negative))
        return false;
    (void)negative;
    group = group_for_name(name);
    if (group != WG_NONE) {
        apply_promotion_group(w, group, demote);
        if (!demote)
            apply_group(w, group, true);
        return true;
    }

    if (!info)
        return false;
    w->promoted[info->id] = demote ? WS_OFF : WS_ON;
    w->promoted_priority[info->id] = 3;
    if (!demote) {
        w->enabled[info->id] = WS_ON; /* GCC: -Werror=foo implies -Wfoo */
        w->enabled_priority[info->id] = 3;
    }
    return true;
}

bool warn_flag(WarnCtx *w, const char *arg)
{
    const char *p;
    const WarnInfo *info;
    char name[128];
    bool negative;
    unsigned group;

    if (!w || !arg)
        return false;
    if (strcmp(arg, "-w") == 0 || strcmp(arg, "w") == 0) {
        w->inhibit = true; /* absolute, deliberately never cleared */
        return true;
    }
    if (strcmp(arg, "-pedantic") == 0 || strcmp(arg, "pedantic") == 0) {
        w->pedantic = 1;
        return true;
    }
    if (strcmp(arg, "-pedantic-errors") == 0 ||
        strcmp(arg, "pedantic-errors") == 0) {
        w->pedantic = 2;
        return true;
    }
    p = strip_w(arg);
    {
        int format_level = format_option_level(p);

        if (format_level == -1)
            return false;
        if (format_level >= 0) {
            apply_format_level(w, (unsigned)format_level);
            return true;
        }
    }
    if (strcmp(p, "error") == 0) {
        w->global_error = true;
        return true;
    }
    if (strcmp(p, "no-error") == 0) {
        w->global_error = false;
        return true;
    }
    /* Accepted for GCC driver compatibility. Stopping after the first hard
     * error is deliberately outside Sprint 37's warning-policy contract. */
    if (strcmp(p, "fatal-errors") == 0 || strcmp(p, "no-fatal-errors") == 0)
        return true;
    if (strncmp(p, "error=", 6) == 0)
        return parse_named_error(w, p + 6, false);
    if (strncmp(p, "no-error=", 9) == 0)
        return parse_named_error(w, p + 9, true);
    if (!normalized_name(p, name, sizeof(name), &negative))
        return false;
    if (strcmp(name, "system-headers") == 0) {
        w->system_headers = !negative;
        return true;
    }
    group = group_for_name(name);
    if (group != WG_NONE) {
        apply_group(w, group, !negative);
        if (group == WG_PEDANTIC)
            w->pedantic = negative ? 0u : 1u;
        return true;
    }
    info = warn_info_for_flag(name);
    if (!info)
        return false;
    w->enabled[info->id] = negative ? WS_OFF : WS_ON;
    w->enabled_priority[info->id] = 3;
    return true;
}

static bool default_enabled(const WarnInfo *info)
{
    return info->default_state == WD_ON;
}

/* Returns -1 suppressed, DIAG_WARNING, or DIAG_ERROR. */
static int base_class(const WarnCtx *w, WarnId id, bool emission_pedwarn)
{
    const WarnInfo *info = warn_info_for_id(id);
    bool pedwarn;
    bool on;

    if (!info)
        return -1;
    pedwarn = emission_pedwarn || info->level == WL_PEDWARN;
    if ((info->groups & (WG_EXTRA | WG_UNUSED)) == (WG_EXTRA | WG_UNUSED) &&
        w->enabled_priority[id] < 3) {
        WarnSetting unused = w->group_enabled[group_index(WG_UNUSED)];

        if (unused == WS_UNSET)
            unused = w->group_enabled[group_index(WG_ALL)];
        on =
            w->group_enabled[group_index(WG_EXTRA)] == WS_ON && unused == WS_ON;
    } else {
        on = w->enabled[id] == WS_UNSET ? default_enabled(info)
                                        : w->enabled[id] == WS_ON;
    }
    if (pedwarn && w->enabled[id] == WS_UNSET && w->pedantic)
        on = true;
    if (!on)
        return -1;
    if (pedwarn && w->pedantic == 2)
        return DIAG_ERROR;
    if (w->promoted[id] == WS_ON)
        return DIAG_ERROR;
    if (w->promoted[id] == WS_OFF)
        return DIAG_WARNING;
    return w->global_error ? DIAG_ERROR : DIAG_WARNING;
}

static size_t event_limit(const WarnCtx *w, u32 seq)
{
    size_t lo = 0, hi = w->events_len;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (w->events[mid].seq <= seq)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static int effective_class(const WarnCtx *w, WarnId id, Span sp,
                           bool emission_pedwarn)
{
    signed char state[WARN_COUNT];
    size_t i, end = event_limit(w, sp.seq);

    memset(state, -1, sizeof(state));
    for (i = 0; i < end; i++) {
        const WarnEvent *ev = &w->events[i];
        if (ev->kind == EV_SET)
            state[ev->id] = (signed char)ev->classification;
        else if (ev->kind == EV_POP)
            memcpy(state, ev->snapshot, sizeof(state));
    }
    if (state[id] == WARN_PRAGMA_IGNORED)
        return -1;
    if (state[id] == WARN_PRAGMA_WARNING)
        return DIAG_WARNING;
    if (state[id] == WARN_PRAGMA_ERROR)
        return DIAG_ERROR;
    return base_class(w, id, emission_pedwarn);
}

static bool suppressed_by_origin(const WarnCtx *w, Span sp, unsigned emit_flags)
{
    if (!w->system_headers &&
        (sp.origin & (SPAN_ORIGIN_SYSTEM_SPELLING | SPAN_ORIGIN_SYSTEM_MACRO)))
        return true;
    return (emit_flags & WARN_SUPPRESS_IN_MACRO) &&
           (sp.origin & SPAN_ORIGIN_ANY_MACRO);
}

bool warn_enabled(const WarnCtx *w, WarnId id, Span sp)
{
    int cls;

    if (!w || id <= WARN_NONE || id >= WARN_COUNT)
        return false;
    cls = effective_class(w, id, sp, false);
    if (cls < 0 || w->inhibit)
        return false;
    return !suppressed_by_origin(w, sp, WARN_EMIT_NONE);
}

static void emit_warning_v(WarnCtx *w, WarnId id, Span sp, unsigned emit_flags,
                           bool emission_pedwarn, const char *fmt, va_list ap)
{
    int cls;

    if (!w || id <= WARN_NONE || id >= WARN_COUNT)
        return;
    cls = effective_class(w, id, sp, emission_pedwarn);
    if (cls < 0 || w->inhibit || suppressed_by_origin(w, sp, emit_flags))
        return;
    diag_emit_warn_v(w->diag, (DiagLevel)cls, sp, id, fmt, ap);
}

void warn_at_v(WarnCtx *w, WarnId id, Span sp, const char *fmt, va_list ap)
{
    emit_warning_v(w, id, sp, WARN_EMIT_NONE, false, fmt, ap);
}

void warn_at(WarnCtx *w, WarnId id, Span sp, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    warn_at_v(w, id, sp, fmt, ap);
    va_end(ap);
}

void warn_pedwarn_at(WarnCtx *w, WarnId id, Span sp, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    emit_warning_v(w, id, sp, WARN_EMIT_NONE, true, fmt, ap);
    va_end(ap);
}

void warn_at_ex(WarnCtx *w, WarnId id, Span sp, unsigned emit_flags,
                const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    emit_warning_v(w, id, sp, emit_flags, false, fmt, ap);
    va_end(ap);
}

static void append_event(WarnCtx *w, WarnEvent ev)
{
    if (w->events_len && ev.seq < w->events[w->events_len - 1].seq)
        CGF_ICE("warning pragma events are not in lexical order");
    if (w->events_len == w->events_cap) {
        size_t cap = w->events_cap ? w->events_cap * 2 : 16;
        WarnEvent *grown =
            arena_alloc(w->arena, cap * sizeof(*grown), _Alignof(WarnEvent));
        if (w->events_len)
            memcpy(grown, w->events, w->events_len * sizeof(*grown));
        w->events = grown;
        w->events_cap = cap;
    }
    w->events[w->events_len++] = ev;
}

static signed char *copy_pragma_state(WarnCtx *w)
{
    signed char *copy = arena_alloc(w->arena, sizeof(w->pragma_state), 1);
    memcpy(copy, w->pragma_state, sizeof(w->pragma_state));
    return copy;
}

void warn_pragma_push(WarnCtx *w, u32 seq)
{
    WarnEvent ev = {0};

    if (w->stack_len == w->stack_cap) {
        size_t cap = w->stack_cap ? w->stack_cap * 2 : 8;
        const signed char **grown = arena_alloc(w->arena, cap * sizeof(*grown),
                                                _Alignof(const signed char *));
        if (w->stack_len)
            memcpy(grown, w->pragma_stack, w->stack_len * sizeof(*grown));
        w->pragma_stack = grown;
        w->stack_cap = cap;
    }
    w->pragma_stack[w->stack_len++] = copy_pragma_state(w);
    ev.seq = seq;
    ev.kind = EV_PUSH;
    append_event(w, ev);
}

bool warn_pragma_pop(WarnCtx *w, u32 seq, Span at)
{
    WarnEvent ev = {0};

    if (w->stack_len == 0) {
        /* GCC restores the command-line baseline for an unmatched pop. It
         * deliberately emits no -Wpragmas diagnostic. */
        (void)at;
        memset(w->pragma_state, -1, sizeof(w->pragma_state));
        ev.seq = seq;
        ev.kind = EV_POP;
        ev.snapshot = copy_pragma_state(w);
        append_event(w, ev);
        return false;
    }
    memcpy(w->pragma_state, w->pragma_stack[--w->stack_len],
           sizeof(w->pragma_state));
    ev.seq = seq;
    ev.kind = EV_POP;
    ev.snapshot = copy_pragma_state(w);
    append_event(w, ev);
    return true;
}

void warn_pragma_set(WarnCtx *w, u32 seq, WarnId id,
                     WarnPragmaClass classification)
{
    WarnEvent ev = {0};

    if (!w || id <= WARN_NONE || id >= WARN_COUNT)
        return;
    w->pragma_state[id] = (signed char)classification;
    ev.seq = seq;
    ev.kind = EV_SET;
    ev.id = id;
    ev.classification = classification;
    append_event(w, ev);
}

bool warn_pragma_set_flag(WarnCtx *w, u32 seq, const char *flag,
                          WarnPragmaClass classification)
{
    const WarnInfo *info = warn_info_for_flag(flag);

    if (!info)
        return false;
    warn_pragma_set(w, seq, info->id, classification);
    return true;
}
