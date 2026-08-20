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
    WarnSetting group_enabled[10];
    signed char pragma_state[WARN_COUNT]; /* -1 or WarnPragmaClass */
    const signed char **pragma_stack;
    size_t stack_len, stack_cap;
    WarnEvent *events;
    size_t events_len, events_cap;
    unsigned pedantic; /* 0 off, 1 warn, 2 errors */
    bool global_error;
    bool inhibit;
    bool system_headers;
    unsigned char implicit_fallthrough_level;
    bool implicit_fallthrough_explicit;
    bool maybe_uninitialized_strict;
    bool fsafe_required;
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

    /* GCC spells the base checker as a parameterized family in diagnostics
     * even when enabled by bare -Wformat.  Parsing still uses the canonical
     * registry key "format". */
    if (id == WARN_FORMAT || id == WARN_FORMAT_SIGNEDNESS)
        return "format=";
    return info ? info->flag : NULL;
}

static unsigned group_for_name(const char *name)
{
    static const struct {
        const char *name;
        unsigned group;
    } groups[] = {{"all", WG_ALL},
                  {"extra", WG_EXTRA},
                  {"unused", WG_UNUSED},
                  {"format", WG_FORMAT},
                  {"implicit", WG_IMPLICIT},
                  {"pedantic", WG_PEDANTIC},
                  {"cgf-ext", WG_CGF_EXT},
                  {"mem", WG_MEM},
                  {"mem-strict", WG_MEM_STRICT}};
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

/* Returns the requested GCC implicit-fallthrough level, -1 for an invalid
 * level option, or -2 when `p` is not an implicit-fallthrough option. */
static int implicit_fallthrough_option_level(const char *p)
{
    if (strcmp(p, "implicit-fallthrough") == 0)
        return 3;
    if (strcmp(p, "no-implicit-fallthrough") == 0)
        return 0;
    if (strncmp(p, "no-implicit-fallthrough=", 24) == 0)
        return -1;
    if (strncmp(p, "implicit-fallthrough=", 21) != 0)
        return -2;
    if (p[21] >= '0' && p[21] <= '5' && p[22] == '\0')
        return p[21] - '0';
    return -1;
}

/* Extension: the bare flag is GCC-compatible; `=strict` asks the flow
 * checker to report predicate/loop cases that the conservative default
 * deliberately leaves undecided. */
static int maybe_uninitialized_option_mode(const char *p)
{
    if (strcmp(p, "maybe-uninitialized") == 0)
        return 0;
    if (strcmp(p, "no-maybe-uninitialized") == 0)
        return 1;
    if (strcmp(p, "maybe-uninitialized=strict") == 0)
        return 2;
    if (strncmp(p, "maybe-uninitialized=", 20) == 0 ||
        strncmp(p, "no-maybe-uninitialized=", 23) == 0)
        return -1;
    return -2;
}

static bool bad_format_subflag_parameter(const char *p)
{
    const char *eq = strchr(p, '=');

    return eq && strncmp(p, "format-", 7) == 0;
}

WarnOptionDisposition warn_option_classify(const char *arg)
{
    char name[128];
    bool negative;
    const char *p;

    if (!arg)
        return WARN_OPTION_UNKNOWN_POSITIVE;
    if (strcmp(arg, "-w") == 0 || strcmp(arg, "w") == 0 ||
        strcmp(arg, "-pedantic") == 0 || strcmp(arg, "pedantic") == 0 ||
        strcmp(arg, "-pedantic-errors") == 0 ||
        strcmp(arg, "pedantic-errors") == 0)
        return WARN_OPTION_KNOWN;
    p = strip_w(arg);
    {
        int format_level = format_option_level(p);

        if (format_level != -2)
            return format_level >= 0 ? WARN_OPTION_KNOWN
                                     : WARN_OPTION_BAD_FORMAT_LEVEL;
    }
    {
        int fallthrough_level = implicit_fallthrough_option_level(p);

        if (fallthrough_level != -2)
            return fallthrough_level >= 0
                       ? WARN_OPTION_KNOWN
                       : WARN_OPTION_BAD_IMPLICIT_FALLTHROUGH_LEVEL;
    }
    {
        int maybe_mode = maybe_uninitialized_option_mode(p);

        if (maybe_mode != -2)
            return maybe_mode >= 0 ? WARN_OPTION_KNOWN
                                   : WARN_OPTION_BAD_MAYBE_UNINITIALIZED_LEVEL;
    }
    if (strncmp(p, "error=", 6) == 0) {
        int format_level = format_option_level(p + 6);
        int fallthrough_level = implicit_fallthrough_option_level(p + 6);

        if (format_level != -2)
            return format_level >= 0 ? WARN_OPTION_KNOWN
                                     : WARN_OPTION_UNKNOWN_PROMOTION;
        if (fallthrough_level == -1)
            return WARN_OPTION_UNKNOWN_PROMOTION;
        if (bad_format_subflag_parameter(p + 6))
            return WARN_OPTION_UNKNOWN_PROMOTION;
        return group_for_name(p + 6) != WG_NONE ||
                       warn_info_for_flag(p + 6) != NULL
                   ? WARN_OPTION_KNOWN
                   : WARN_OPTION_UNKNOWN_PROMOTION;
    }
    if (strncmp(p, "no-error=", 9) == 0) {
        int format_level = format_option_level(p + 9);
        int fallthrough_level = implicit_fallthrough_option_level(p + 9);

        if (format_level != -2)
            return format_level >= 0 ? WARN_OPTION_KNOWN
                                     : WARN_OPTION_UNKNOWN_PROMOTION;
        if (fallthrough_level == -1)
            return WARN_OPTION_UNKNOWN_PROMOTION;
        if (bad_format_subflag_parameter(p + 9))
            return WARN_OPTION_UNKNOWN_PROMOTION;
        return group_for_name(p + 9) != WG_NONE ||
                       warn_info_for_flag(p + 9) != NULL
                   ? WARN_OPTION_KNOWN
                   : WARN_OPTION_UNKNOWN_PROMOTION;
    }
    if (!normalized_name(arg, name, sizeof(name), &negative))
        return WARN_OPTION_UNKNOWN_POSITIVE;
    if (bad_format_subflag_parameter(p))
        return negative ? WARN_OPTION_UNKNOWN_NEGATIVE
                        : WARN_OPTION_UNKNOWN_POSITIVE;
    if (strcmp(name, "error") == 0 || strcmp(name, "fatal-errors") == 0 ||
        strcmp(name, "system-headers") == 0)
        return WARN_OPTION_KNOWN;
    if (group_for_name(name) != WG_NONE || warn_info_for_flag(name) != NULL)
        return WARN_OPTION_KNOWN;
    return negative ? WARN_OPTION_UNKNOWN_NEGATIVE
                    : WARN_OPTION_UNKNOWN_POSITIVE;
}

bool warn_option_known(const char *arg)
{
    return warn_option_classify(arg) == WARN_OPTION_KNOWN;
}

const char *warn_option_bad_value_label(WarnOptionDisposition disposition)
{
    if (disposition == WARN_OPTION_BAD_FORMAT_LEVEL)
        return "-Wformat=";
    if (disposition == WARN_OPTION_BAD_IMPLICIT_FALLTHROUGH_LEVEL)
        return "-Wimplicit-fallthrough=";
    if (disposition == WARN_OPTION_BAD_MAYBE_UNINITIALIZED_LEVEL)
        return "-Wmaybe-uninitialized=";
    return NULL;
}

WarnId warn_pragma_option_id(const char *option)
{
    const WarnInfo *info;
    size_t flag_len;

    if (!option || option[0] != '-' || option[1] != 'W')
        return WARN_NONE;
    info = warn_info_for_flag(option + 2);
    if (!info)
        return WARN_NONE;
    flag_len = strlen(info->flag);
    if (strlen(option) != flag_len + 2 || strcmp(option + 2, info->flag) != 0)
        return WARN_NONE;
    return info->id;
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
    fputs("  -Wmaybe-uninitialized=strict                 [disabled]\n", out);
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
    if (group == WG_EXTRA && !w->implicit_fallthrough_explicit)
        w->implicit_fallthrough_level = on ? 3u : 0u;
    for (i = 1; i < WARN_COUNT; i++) {
        unsigned groups = infos[i].groups;
        unsigned char item_priority = priority;

        if (!(groups & group))
            continue;
        /* -Wformat implies -Wnonnull, but the implication is ordered at the
         * same strength as -Wall: `-Wno-format -Wall` re-enables nonnull,
         * while `-Wall -Wno-format` disables it.  A direct -Wno-nonnull is
         * still priority 3 and wins in either order. */
        if (group == WG_FORMAT && i == WARN_NONNULL)
            item_priority = 1;
        /* GCC C enables these only for the intersection of -Wextra and
         * -Wunused. Encoding both memberships in the row keeps that law
         * out of warning-id special cases. */
        if ((groups & (WG_EXTRA | WG_UNUSED)) == (WG_EXTRA | WG_UNUSED)) {
            /* Their effective state is the conjunction, computed at query
             * time. Keeping group state separate makes -Wno-extra undo the
             * condition even though -Wunused is the more specific group. */
            continue;
        }
        if (w->enabled_priority[i] <= item_priority) {
            w->enabled[i] = on ? WS_ON : WS_OFF;
            w->enabled_priority[i] = item_priority;
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
    int fallthrough_level = implicit_fallthrough_option_level(p);
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
    if (info->id == WARN_IMPLICIT_FALLTHROUGH && fallthrough_level < 0)
        return false;
    w->promoted[info->id] = demote ? WS_OFF : WS_ON;
    w->promoted_priority[info->id] = 3;
    if (!demote) {
        w->enabled[info->id] = WS_ON; /* GCC: -Werror=foo implies -Wfoo */
        w->enabled_priority[info->id] = 3;
        if (info->id == WARN_IMPLICIT_FALLTHROUGH) {
            w->implicit_fallthrough_level = (unsigned char)fallthrough_level;
            w->implicit_fallthrough_explicit = true;
            if (fallthrough_level == 0)
                w->enabled[info->id] = WS_OFF;
        }
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
    if (strcmp(arg, WARN_FSAFE_REQUIRED_OPTION) == 0) {
        w->fsafe_required = true;
        return true;
    }
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
    {
        int fallthrough_level = implicit_fallthrough_option_level(p);

        if (fallthrough_level == -1)
            return false;
        if (fallthrough_level >= 0) {
            w->implicit_fallthrough_level = (unsigned char)fallthrough_level;
            w->implicit_fallthrough_explicit = true;
            w->enabled[WARN_IMPLICIT_FALLTHROUGH] =
                fallthrough_level == 0 ? WS_OFF : WS_ON;
            w->enabled_priority[WARN_IMPLICIT_FALLTHROUGH] = 3;
            return true;
        }
    }
    {
        int maybe_mode = maybe_uninitialized_option_mode(p);

        if (maybe_mode == -1)
            return false;
        if (maybe_mode >= 0) {
            w->enabled[WARN_MAYBE_UNINITIALIZED] =
                maybe_mode == 1 ? WS_OFF : WS_ON;
            w->enabled_priority[WARN_MAYBE_UNINITIALIZED] = 3;
            w->maybe_uninitialized_strict = maybe_mode == 2;
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
    if (strncmp(p, "error=", 6) == 0) {
        int level = format_option_level(p + 6);

        if (level != -2) {
            if (level < 0)
                return false;
            apply_format_level(w, (unsigned)level);
            if (level > 0)
                apply_promotion_group(w, WG_FORMAT, false);
            if (level == 2)
                apply_promotion_group(w, WG_FORMAT2, false);
            return true;
        }
        if (bad_format_subflag_parameter(p + 6))
            return false;
        return parse_named_error(w, p + 6, false);
    }
    if (strncmp(p, "no-error=", 9) == 0) {
        int level = format_option_level(p + 9);

        if (level != -2) {
            if (level < 0)
                return false;
            w->promoted[WARN_FORMAT] = WS_OFF;
            w->promoted_priority[WARN_FORMAT] = 3;
            return true;
        }
        if (bad_format_subflag_parameter(p + 9))
            return false;
        return parse_named_error(w, p + 9, true);
    }
    if (!normalized_name(p, name, sizeof(name), &negative))
        return false;
    if (bad_format_subflag_parameter(p))
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

static bool fsafe_required_warning(WarnId id)
{
    const WarnInfo *info = warn_info_for_id(id);

    return info && ((info->groups & WG_MEM) || id == WARN_UNINITIALIZED);
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

static int pragma_class_at(const WarnCtx *w, WarnId id, u32 seq)
{
    signed char state[WARN_COUNT];
    size_t i, end = event_limit(w, seq);

    memset(state, -1, sizeof(state));
    for (i = 0; i < end; i++) {
        const WarnEvent *ev = &w->events[i];

        if (ev->kind == EV_SET)
            state[ev->id] = (signed char)ev->classification;
        else if (ev->kind == EV_POP)
            memcpy(state, ev->snapshot, sizeof(state));
    }
    return state[id];
}

static int effective_class(const WarnCtx *w, WarnId id, Span sp,
                           bool emission_pedwarn)
{
    int pragma_class = pragma_class_at(w, id, sp.seq);

    /* MS-C-01: -fsafe's documented diagnostic floor is a policy invariant,
     * so exact/group opt-outs and lexical pragmas cannot suppress or demote
     * required memory and definite-initialization errors. */
    if (w->fsafe_required && fsafe_required_warning(id))
        return DIAG_ERROR;
    if (pragma_class == WARN_PRAGMA_IGNORED)
        return -1;
    if (pragma_class == WARN_PRAGMA_WARNING)
        return DIAG_WARNING;
    if (pragma_class == WARN_PRAGMA_ERROR)
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

bool warn_explicitly_enabled(const WarnCtx *w, WarnId id, Span sp)
{
    const WarnInfo *info;
    int pragma_class;
    bool on;

    if (!w || id <= WARN_NONE || id >= WARN_COUNT || w->inhibit ||
        suppressed_by_origin(w, sp, WARN_EMIT_NONE))
        return false;
    if (w->fsafe_required && fsafe_required_warning(id))
        return true;
    pragma_class = pragma_class_at(w, id, sp.seq);
    if (pragma_class == WARN_PRAGMA_IGNORED)
        return false;
    if (pragma_class == WARN_PRAGMA_WARNING ||
        pragma_class == WARN_PRAGMA_ERROR)
        return true;
    info = warn_info_for_id(id);
    if ((info->groups & (WG_EXTRA | WG_UNUSED)) == (WG_EXTRA | WG_UNUSED) &&
        w->enabled_priority[id] < 3) {
        WarnSetting unused = w->group_enabled[group_index(WG_UNUSED)];

        if (unused == WS_UNSET)
            unused = w->group_enabled[group_index(WG_ALL)];
        on =
            w->group_enabled[group_index(WG_EXTRA)] == WS_ON && unused == WS_ON;
    } else {
        on = w->enabled[id] == WS_ON;
    }
    if (info->level == WL_PEDWARN && w->enabled[id] == WS_UNSET && w->pedantic)
        on = true;
    return on;
}

unsigned warn_implicit_fallthrough_level(const WarnCtx *w)
{
    return w ? w->implicit_fallthrough_level : 0;
}

bool warn_maybe_uninitialized_strict(const WarnCtx *w)
{
    return w && w->maybe_uninitialized_strict;
}

static bool is_flow_warning(WarnId id)
{
    return id == WARN_UNINITIALIZED || id == WARN_MAYBE_UNINITIALIZED ||
           id == WARN_INIT_SELF || id == WARN_RETURN_TYPE ||
           id == WARN_UNREACHABLE_CODE || id == WARN_INFINITE_RECURSION ||
           id == WARN_SWITCH_UNREACHABLE;
}

bool warn_flow_needed(const WarnCtx *w)
{
    static const WarnId ids[] = {
        WARN_UNINITIALIZED,      WARN_MAYBE_UNINITIALIZED,
        WARN_INIT_SELF,          WARN_RETURN_TYPE,
        WARN_UNREACHABLE_CODE,   WARN_INFINITE_RECURSION,
        WARN_SWITCH_UNREACHABLE,
    };
    Span none = {0};
    size_t i;

    if (!w || w->inhibit)
        return false;
    for (i = 0; i < CGF_ARRAY_LEN(ids); i++)
        if (warn_enabled(w, ids[i], none))
            return true;
    /* A source pragma may enable an otherwise-off extension after seq 0.
     * Retain the analysis in that case; per-occurrence policy still decides
     * whether an individual diagnostic is emitted. */
    for (i = 0; i < w->events_len; i++)
        if (w->events[i].kind == EV_SET && is_flow_warning(w->events[i].id) &&
            w->events[i].classification != WARN_PRAGMA_IGNORED)
            return true;
    return false;
}

static bool is_memsafe_warning(WarnId id)
{
    return id == WARN_MEM_ANNOTATION_MISMATCH || id == WARN_MEM_DOUBLE_FREE ||
           id == WARN_MEM_FREE_NONHEAP || id == WARN_MEM_LEAK ||
           id == WARN_MEM_NULL_CHECK || id == WARN_MEM_OUT_OF_BOUNDS ||
           id == WARN_MEM_REALLOC_ZERO || id == WARN_MEM_SIZEOF_MISMATCH ||
           id == WARN_MEM_SUGGEST_ANNOTATIONS ||
           id == WARN_MEM_UNBOUNDED_COPY || id == WARN_MEM_UNINIT_READ ||
           id == WARN_MEM_USE_AFTER_FREE ||
           id == WARN_MEM_USE_AFTER_FREE_UNKNOWN;
}

bool warn_memsafe_needed(const WarnCtx *w)
{
    static const WarnId ids[] = {
        WARN_MEM_ANNOTATION_MISMATCH,
        WARN_MEM_DOUBLE_FREE,
        WARN_MEM_FREE_NONHEAP,
        WARN_MEM_LEAK,
        WARN_MEM_NULL_CHECK,
        WARN_MEM_OUT_OF_BOUNDS,
        WARN_MEM_REALLOC_ZERO,
        WARN_MEM_SIZEOF_MISMATCH,
        WARN_MEM_SUGGEST_ANNOTATIONS,
        WARN_MEM_UNBOUNDED_COPY,
        WARN_MEM_UNINIT_READ,
        WARN_MEM_USE_AFTER_FREE,
        WARN_MEM_USE_AFTER_FREE_UNKNOWN,
    };
    Span none = {0};
    size_t i;

    if (!w || w->inhibit)
        return false;
    for (i = 0; i < CGF_ARRAY_LEN(ids); i++)
        if (warn_enabled(w, ids[i], none))
            return true;
    /* A source pragma can re-enable an individual checker after -Wno-mem.
     * Retain analysis so occurrence policy can honor that lexical event. */
    for (i = 0; i < w->events_len; i++)
        if (w->events[i].kind == EV_SET &&
            is_memsafe_warning(w->events[i].id) &&
            w->events[i].classification != WARN_PRAGMA_IGNORED)
            return true;
    return false;
}

static bool is_memsafe_autofix_warning(WarnId id)
{
    return id == WARN_MEM_NULL_CHECK || id == WARN_MEM_SIZEOF_MISMATCH ||
           id == WARN_MEM_UNBOUNDED_COPY;
}

bool warn_memsafe_autofix_needed(const WarnCtx *w)
{
    static const WarnId ids[] = {
        WARN_MEM_NULL_CHECK,
        WARN_MEM_SIZEOF_MISMATCH,
        WARN_MEM_UNBOUNDED_COPY,
    };
    Span none = {0};
    size_t i;

    if (!w || w->inhibit)
        return false;
    for (i = 0; i < CGF_ARRAY_LEN(ids); i++)
        if (warn_enabled(w, ids[i], none))
            return true;
    /* Pragmas can enable an otherwise-off autofix checker after seq 0. */
    for (i = 0; i < w->events_len; i++)
        if (w->events[i].kind == EV_SET &&
            is_memsafe_autofix_warning(w->events[i].id) &&
            w->events[i].classification != WARN_PRAGMA_IGNORED)
            return true;
    return false;
}

bool warn_mem_strict_enabled(const WarnCtx *w)
{
    Span none = {0};

    return w && (warn_enabled(w, WARN_MEM_NULL_CHECK, none) ||
                 warn_enabled(w, WARN_MEM_SIZEOF_MISMATCH, none) ||
                 warn_enabled(w, WARN_MEM_UNBOUNDED_COPY, none) ||
                 warn_enabled(w, WARN_MEM_USE_AFTER_FREE_UNKNOWN, none));
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

void warn_at_fixits(WarnCtx *w, WarnId id, Span sp, const DiagFixit *fixits,
                    size_t fixit_count, const char *fmt, ...)
{
    int cls;
    va_list ap, copy;
    char *message;
    int needed;

    if (!w || id <= WARN_NONE || id >= WARN_COUNT)
        return;
    cls = effective_class(w, id, sp, false);
    if (cls < 0 || w->inhibit || suppressed_by_origin(w, sp, WARN_EMIT_NONE))
        return;
    va_start(ap, fmt);
    va_copy(copy, ap);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(ap);
        return;
    }
    message = arena_alloc(w->arena, (size_t)needed + 1, 1);
    vsnprintf(message, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    diag_emit_warn_fixits(w->diag, (DiagLevel)cls, sp, id, fixits, fixit_count,
                          "%s", message);
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
