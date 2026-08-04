#include <stdio.h>
#include <string.h>

#include "driver/args.h"
#include "util/arena.h"
#include "util/dlev.h"
#include "warn/warn.h"

/* Sprint 26: gcc's joined-or-separate zoo (-DFOO vs -D FOO, -ofile vs
 * -o file) demands a spec table, not strcmp chains. Matching is
 * exact-name first, then longest-prefix among joined-capable entries.
 * args_parse does no I/O except response-file reads. */

typedef enum {
    ARG_NONE,     /* -c, -v, -w                                  */
    ARG_JOINED,   /* -Wl,..., -std= (value glued, never separate) */
    ARG_SEPARATE, /* -Xlinker arg, -include file (next argv only) */
    ARG_EITHER,   /* -o/-I/-D/-U/-L/-l/-x/-MF/-MT/-MQ/-B          */
} ArgStyle;

/* Dispatch codes: ONE table row per flag; families share a handler that
 * switches on the code FROM THE TABLE (still table-driven — the string
 * match happens exactly once, in match_flag). */
enum {
    F_HELP,
    F_HELP_WARNINGS,
    F_VERSION,
    F_DUMPVERSION,
    F_DUMPMACHINE,
    F_PRINT_SEARCH,
    F_PRINT_PROG,
    F_PRINT_FILE,
    F_VERBOSE,
    F_DRY_RUN,
    F_MODE_E,
    F_MODE_S,
    F_MODE_C,
    F_OUTPUT,
    F_XLANG,
    F_NO_LINEMARKERS,
    F_DUMP_MACROS,
    F_DUMP_TOKENS,
    F_DUMP_AST,
    F_DUMP_SEMA,
    F_DUMP_LAYOUT,
    F_DUMP_INIT,
    F_SYNTAX_ONLY,
    F_EMIT_IR,
    F_EMIT_MIR,
    F_TRIGRAPHS,
    F_STD,
    F_NOSTDINC,
    F_DIR_I,
    F_DIR_IQUOTE,
    F_DIR_ISYSTEM,
    F_INCLUDE,
    F_DEFINE,
    F_UNDEF,
    F_DEP_M,
    F_DEP_MM,
    F_DEP_MD,
    F_DEP_MMD,
    F_DEP_MF,
    F_DEP_MT,
    F_DEP_MQ,
    F_DEP_MP,
    F_OPT_O,
    F_TIME_REPORT,
    F_WSUPPRESS,
    F_WERROR,
    F_WERROR_EQ,
    F_WGENERAL,
    F_WL,
    F_PEDANTIC,
    F_PEDANTIC_ERR,
    F_MAX_ERRORS,
    F_FCOMMON,
    F_FNO_COMMON,
    F_FFREESTANDING,
    F_FHOSTED,
    F_FWRAPV,
    F_FSAFE,
    F_FCGF_SAFE,
    F_FNO_CGF_SAFE,
    F_FSAFE_ALLOW_UNSAFE,
    F_FDIAG_PARSEABLE_FIXITS,
    F_FDIAG_APPLY_FIXITS,
    F_FTRIVIAL_AUTO_VAR_INIT,
    F_FSTRICT_ALIAS,
    F_FNO_STRICT_ALIAS,
    F_FOMIT_FP,
    F_FNO_OMIT_FP,
    F_FFAST_MATH,
    F_FNO_FAST_MATH,
    F_FASSOCIATIVE_MATH,
    F_FNO_ASSOCIATIVE_MATH,
    F_FSIGNED_ZEROS,
    F_FNO_SIGNED_ZEROS,
    F_FFINITE_MATH,
    F_FNO_FINITE_MATH,
    F_FRECIPROCAL_MATH,
    F_FNO_RECIPROCAL_MATH,
    F_FMATH_ERRNO,
    F_FNO_MATH_ERRNO,
    F_FFP_CONTRACT,
    F_FGENERAL,
    F_DEBUG_G,
    F_SHARED,
    F_PIC,
    F_DIR_L,
    F_LIB,
    F_XLINKER,
    F_STATIC,
    F_NOSTDLIB,
    F_NOSTARTFILES,
    F_NODEFAULTLIBS,
    F_DIR_B,
};

typedef struct FlagSpec FlagSpec;
struct FlagSpec {
    const char *name;
    ArgStyle style;
    bool (*handle)(DriverArgs *da, const FlagSpec *fs, const char *val);
    int code;
};

/* ---- small helpers -------------------------------------------------- */

static char *astr(struct Arena *ar, const char *s, size_t n)
{
    char *p = arena_alloc(ar, n + 1, 1);

    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* Parse-time state that is not part of the result: the active -x
 * language and the arena for built strings. */
typedef struct {
    struct Arena *arena;
    int x_lang; /* -1 = extension dispatch; IN_C when "-x c" is active */
} ParseState;

static ParseState *g_ps; /* set for the duration of one args_parse call */

/* Make-quoting for -MQ targets and depfile prerequisite paths:
 * $ -> $$, space -> backslash-space (with any RUN of preceding
 * backslashes doubled, or Make un-escapes wrongly), # -> \#. */
const char *cgf_make_quote(struct Arena *ar, const char *s)
{
    size_t n = strlen(s), extra = 0, i, bs;
    char *out, *w;

    for (i = 0; i < n; i++) {
        if (s[i] == '$' || s[i] == '#')
            extra++;
        else if (s[i] == ' ') {
            extra++;
            for (bs = i; bs > 0 && s[bs - 1] == '\\'; bs--)
                extra++;
        }
    }
    if (!extra)
        return s;
    out = arena_alloc(ar, n + extra + 1, 1);
    w = out;
    for (i = 0; i < n; i++) {
        char c = s[i];

        if (c == '$') {
            *w++ = '$';
            *w++ = '$';
        } else if (c == '#') {
            *w++ = '\\';
            *w++ = '#';
        } else if (c == ' ') {
            /* Double the run of backslashes immediately before the
             * space, then escape the space itself. */
            for (bs = i; bs > 0 && s[bs - 1] == '\\'; bs--)
                *w++ = '\\';
            *w++ = '\\';
            *w++ = ' ';
        } else {
            *w++ = c;
        }
    }
    *w = '\0';
    return out;
}

/* ---- handlers ------------------------------------------------------- */

static bool h_info(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    switch (fs->code) {
    case F_HELP:
        da->show_help = true;
        break;
    case F_HELP_WARNINGS:
        da->show_help_warnings = true;
        break;
    case F_VERSION:
        da->show_version = true;
        break;
    case F_DUMPVERSION:
        da->show_dumpversion = true;
        break;
    case F_DUMPMACHINE:
        da->show_dumpmachine = true;
        break;
    case F_PRINT_SEARCH:
        da->print_search_dirs = true;
        break;
    case F_PRINT_PROG:
        da->print_prog = val;
        break;
    case F_PRINT_FILE:
        da->print_file = val;
        break;
    case F_VERBOSE:
        da->verbose = true;
        break;
    case F_DRY_RUN:
        da->dry_run = true;
        break;
    }
    return true;
}

static bool h_mode(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    (void)val;
    switch (fs->code) {
    case F_MODE_E:
        da->mode_E = true;
        break;
    case F_MODE_S:
        da->emit_asm = true;
        break;
    case F_MODE_C:
        da->compile_obj = true;
        break;
    case F_NO_LINEMARKERS:
        da->no_linemarkers = true;
        break;
    case F_DUMP_MACROS:
        da->dump_macros = true;
        break;
    case F_DUMP_TOKENS:
        da->dump_tokens = true;
        break;
    case F_DUMP_AST:
        da->dump_ast = true;
        break;
    case F_DUMP_SEMA:
        da->dump_sema = true;
        break;
    case F_DUMP_LAYOUT:
        da->dump_layout = true;
        break;
    case F_DUMP_INIT:
        da->dump_init = true;
        break;
    case F_SYNTAX_ONLY:
        da->syntax_only = true;
        break;
    case F_EMIT_IR:
        da->emit_ir = true;
        break;
    case F_EMIT_MIR:
        da->emit_mir = true;
        break;
    case F_TRIGRAPHS:
        da->trigraphs = true;
        break;
    case F_TIME_REPORT:
        da->time_report = true;
        break;
    case F_NOSTDINC:
        da->nostdinc = true;
        break;
    }
    return true;
}

static bool h_output(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    (void)fs;
    da->output = val;
    return true;
}

static bool h_xlang(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    (void)fs;
    if (strcmp(val, "c") == 0) {
        g_ps->x_lang = IN_C;
    } else if (strcmp(val, "none") == 0) {
        g_ps->x_lang = -1; /* restore extension dispatch */
    } else if (!da->bad_value) {
        da->bad_value = "-x";
    }
    return true;
}

/* -std= values: gcc's alias zoo. The default stays c17 (locked decision;
 * gcc defaults to gnu17 — divergence documented in --help). CStd codes
 * match lex.h: 0 c89, 1 c99, 2 c11, 3 c17, 4.. the gnu twins. */
static bool h_std(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    static const struct {
        const char *name;
        int std;
    } stds[] = {
        {"c89", 0},
        {"c90", 0},
        {"iso9899:1990", 0},
        {"iso9899:199409", 0}, /* C95: tracked as c89 until Sprint 55 */
        {"c99", 1},
        {"c9x", 1},
        {"iso9899:1999", 1},
        {"iso9899:199x", 1},
        {"c11", 2},
        {"c1x", 2},
        {"iso9899:2011", 2},
        {"c17", 3},
        {"c18", 3},
        {"iso9899:2017", 3},
        {"iso9899:2018", 3},
        {"gnu89", 4},
        {"gnu90", 4},
        {"gnu99", 5},
        {"gnu9x", 5},
        {"gnu11", 6},
        {"gnu1x", 6},
        {"gnu17", 7},
        {"gnu18", 7},
    };
    size_t i;

    (void)fs;
    for (i = 0; i < CGF_ARRAY_LEN(stds); i++) {
        if (strcmp(val, stds[i].name) == 0) {
            da->std = stds[i].std;
            return true;
        }
    }
    if (!da->bad_value)
        da->bad_value = "-std=";
    return true;
}

static bool h_dir(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    switch (fs->code) {
    case F_DIR_I:
        VecStr_push(&da->include_dirs, val);
        break;
    case F_DIR_IQUOTE:
        VecStr_push(&da->iquote_dirs, val);
        break;
    case F_DIR_ISYSTEM:
        VecStr_push(&da->isystem_dirs, val);
        break;
    case F_DIR_L:
        VecStr_push(&da->lib_dirs, val);
        break;
    case F_DIR_B:
        VecStr_push(&da->prefix_dirs, val);
        break;
    case F_INCLUDE:
        VecStr_push(&da->pre_includes, val);
        break;
    }
    return true;
}

static bool h_def(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    CmdDefine d;

    d.val = val;
    d.is_undef = fs->code == F_UNDEF;
    VecDef_push(&da->defs, d);
    return true;
}

static bool h_dep(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    switch (fs->code) {
    case F_DEP_M:
        da->dep_mode = DEP_M;
        break;
    case F_DEP_MM:
        da->dep_mode = DEP_MM;
        break;
    case F_DEP_MD:
        da->dep_mode = DEP_M;
        da->dep_side = true;
        break;
    case F_DEP_MMD:
        da->dep_mode = DEP_MM;
        da->dep_side = true;
        break;
    case F_DEP_MF:
        da->dep_file = val;
        break;
    case F_DEP_MT:
        VecStr_push(&da->dep_targets, val);
        break;
    case F_DEP_MQ:
        VecStr_push(&da->dep_targets, cgf_make_quote(g_ps->arena, val));
        break;
    case F_DEP_MP:
        da->dep_phony = true;
        break;
    }
    return true;
}

static bool h_opt(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    bool valid = true;

    (void)fs;
    if (val[0] == '\0') {
        da->opt_level = OPT_O1;
    } else if (val[0] >= '0' && val[0] <= '9' && val[1] == '\0') {
        int n = val[0] - '0';

        da->opt_level = n >= 3 ? OPT_O3 : (OptLevel)n;
    } else if (strcmp(val, "s") == 0 || strcmp(val, "z") == 0) {
        da->opt_level = OPT_OS;
    } else if (strcmp(val, "fast") == 0) {
        da->opt_level = OPT_OFAST;
    } else if (strcmp(val, "g") == 0) {
        da->opt_level = OPT_O1; /* gcc's -Og; our nearest stored level */
    } else if (val[0] >= '0' && val[0] <= '9') {
        da->opt_level = OPT_O3; /* -O7: gcc clamps, so do we */
    } else {
        if (!da->bad_value)
            da->bad_value = "-O";
        valid = false;
    }
    if (valid)
        da->fast_math = da->opt_level == OPT_OFAST;
    return true;
}

static bool h_debug(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    (void)fs;
    if (val[0] == '\0') {
        da->debug_level = 2;
    } else if (val[1] == '\0' && val[0] >= '0' && val[0] <= '3') {
        da->debug_level = (u8)(val[0] - '0');
    } else if (!da->bad_value) {
        da->bad_value = "-g";
    }
    return true;
}

static bool h_warn(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    WarnOpt w;

    switch (fs->code) {
    case F_WSUPPRESS:
        da->no_warnings = true;
        return true;
    case F_WERROR:
        da->werror = true;
        w.name = "error";
        VecWarn_push(&da->warn_opts, w);
        return true;
    case F_WERROR_EQ: {
        /* Record "error=NAME" in the ordered warning-policy stream. */
        size_t n = strlen(val);
        char *s = arena_alloc(g_ps->arena, n + 7, 1);

        memcpy(s, "error=", 6);
        memcpy(s + 6, val, n + 1);
        w.name = s;
        VecWarn_push(&da->warn_opts, w);
        if (warn_option_classify(s) != WARN_OPTION_KNOWN && !da->unknown_opt) {
            char *full = arena_alloc(g_ps->arena, n + 9, 1);

            memcpy(full, "-Werror=", 8);
            memcpy(full + 8, val, n + 1);
            da->unknown_opt = full;
        }
        return true;
    }
    case F_WGENERAL: {
        WarnOptionDisposition disposition = warn_option_classify(val);

        if (disposition == WARN_OPTION_BAD_FORMAT_LEVEL ||
            disposition == WARN_OPTION_BAD_IMPLICIT_FALLTHROUGH_LEVEL ||
            disposition == WARN_OPTION_BAD_MAYBE_UNINITIALIZED_LEVEL) {
            if (!da->bad_value)
                da->bad_value = warn_option_bad_value_label(disposition);
            return true;
        }
        /* -Wno-<unknown> is SILENT (gcc parity — configure probes depend
         * on it; gcc reports it only if another diagnostic fires, which
         * is diagnosed only if another warning fires). A positive
         * unknown warns and continues. */
        w.name = val;
        VecWarn_push(&da->warn_opts, w);
        if (strcmp(val, "no-error") == 0)
            da->werror = false;
        else if (strcmp(val, "pedantic") == 0)
            da->pedantic = true;
        else if (strcmp(val, "no-pedantic") == 0)
            da->pedantic = false;
        if (disposition == WARN_OPTION_UNKNOWN_PROMOTION && !da->unknown_opt) {
            size_t n = strlen(val);
            char *s = arena_alloc(g_ps->arena, n + 3, 1);

            memcpy(s, "-W", 2);
            memcpy(s + 2, val, n + 1);
            da->unknown_opt = s;
        }
        if (disposition == WARN_OPTION_UNKNOWN_POSITIVE) {
            size_t n = strlen(val);
            char *s = arena_alloc(g_ps->arena, n + 3, 1);

            memcpy(s, "-W", 2);
            memcpy(s + 2, val, n + 1);
            VecStr_push(&da->warn_unrecognized, s);
        } else if (disposition == WARN_OPTION_UNKNOWN_NEGATIVE) {
            size_t n = strlen(val);
            char *s = arena_alloc(g_ps->arena, n + 3, 1);

            memcpy(s, "-W", 2);
            memcpy(s + 2, val, n + 1);
            VecStr_push(&da->warn_unknown_negative, s);
        }
        return true;
    }
    case F_PEDANTIC:
        da->pedantic = true;
        w.name = "pedantic";
        VecWarn_push(&da->warn_opts, w);
        return true;
    case F_PEDANTIC_ERR:
        da->pedantic = true;
        da->pedantic_errors = true;
        w.name = "pedantic-errors";
        VecWarn_push(&da->warn_opts, w);
        return true;
    }
    return true;
}

static bool h_max_errors(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    unsigned long n = 0;
    const char *q = val;

    if (!*q) {
        if (!da->bad_value)
            da->bad_value = fs->name;
        return true;
    }
    for (; *q; q++) {
        if (*q < '0' || *q > '9') {
            if (!da->bad_value)
                da->bad_value = fs->name;
            return true;
        }
        n = n * 10 + (unsigned long)(*q - '0');
        if (n > 0xffffffffUL) {
            n = 0xffffffffUL;
            break;
        }
    }
    da->max_errors = (u32)n;
    return true;
}

static bool h_fflag(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    switch (fs->code) {
    case F_FCOMMON:
        da->fno_common = false;
        break;
    case F_FNO_COMMON:
        da->fno_common = true;
        break;
    case F_FFREESTANDING:
        da->freestanding = true;
        break;
    case F_FHOSTED:
        da->freestanding = false;
        break;
    case F_FWRAPV:
        da->fwrapv = true;
        break;
    case F_FSAFE:
        da->fsafe = true;
        break;
    case F_FCGF_SAFE:
        da->fcgf_safe = true;
        da->fcgf_safe_disabled = false;
        break;
    case F_FNO_CGF_SAFE:
        da->fcgf_safe = false;
        da->fcgf_safe_disabled = true;
        break;
    case F_FSAFE_ALLOW_UNSAFE:
        VecStr_push(&da->fsafe_allow_unsafe, val);
        break;
    case F_FDIAG_PARSEABLE_FIXITS:
        da->diagnostics_parseable_fixits = true;
        break;
    case F_FDIAG_APPLY_FIXITS:
        if (val && strcmp(val, "all") == 0)
            da->fixit_apply_mode = FIXIT_APPLY_ALL;
        else if (val && strcmp(val, "interactive") == 0)
            da->fixit_apply_mode = FIXIT_APPLY_INTERACTIVE;
        else if (!da->bad_value)
            da->bad_value = "-fdiagnostics-apply-fixits=";
        break;
    case F_FTRIVIAL_AUTO_VAR_INIT:
        if (val && strcmp(val, "zero") == 0)
            da->trivial_auto_var_init = AUTO_VAR_INIT_ZERO;
        else if (val && strcmp(val, "pattern") == 0)
            da->trivial_auto_var_init = AUTO_VAR_INIT_PATTERN;
        else if (!da->bad_value)
            da->bad_value = "-ftrivial-auto-var-init=";
        break;
    case F_FSTRICT_ALIAS:
        da->fno_strict_aliasing = false;
        break;
    case F_FNO_STRICT_ALIAS:
        da->fno_strict_aliasing = true;
        break;
    case F_FOMIT_FP:
        /* Rejected until post-0.1.0 (breaks Sprint 29 CFI): warn+ignore. */
        VecStr_push(&da->warn_unrecognized, "-fomit-frame-pointer");
        break;
    case F_FNO_OMIT_FP:
        break; /* already the only behavior */
    case F_FFAST_MATH:
        da->fast_math = true;
        break;
    case F_FNO_FAST_MATH:
        da->fast_math = false;
        break;
    case F_FASSOCIATIVE_MATH:
    case F_FNO_ASSOCIATIVE_MATH:
    case F_FSIGNED_ZEROS:
    case F_FNO_SIGNED_ZEROS:
    case F_FFINITE_MATH:
    case F_FNO_FINITE_MATH:
    case F_FRECIPROCAL_MATH:
    case F_FNO_RECIPROCAL_MATH:
    case F_FMATH_ERRNO:
    case F_FNO_MATH_ERRNO:
        VecStr_push(&da->warn_fast_math, fs->name);
        break;
    case F_FFP_CONTRACT: {
        size_t n;
        char *spelling;

        if (strcmp(val, "off") != 0 && strcmp(val, "on") != 0 &&
            strcmp(val, "fast") != 0 &&
            strcmp(val, "fast-honor-pragmas") != 0) {
            if (!da->bad_value)
                da->bad_value = "-ffp-contract=";
            break;
        }
        n = strlen(fs->name) + strlen(val);
        spelling = arena_alloc(g_ps->arena, n + 1, 1);
        memcpy(spelling, fs->name, strlen(fs->name));
        memcpy(spelling + strlen(fs->name), val, strlen(val) + 1);
        VecStr_push(&da->warn_fast_math, spelling);
        break;
    }
    case F_FGENERAL: {
        /* Unknown -f<x>: warning, continue — hard-erroring breaks
         * flag-probing configure scripts. -flto/-fprofile-* are named
         * out-of-scope errors and -fPIC/-fpie name Sprint 51 (never
         * silently accepted: accepting means promising semantics). */
        size_t n = strlen(val);
        char *s = arena_alloc(g_ps->arena, n + 3, 1);

        memcpy(s, "-f", 2);
        memcpy(s + 2, val, n + 1);
        if (strncmp(val, "lto", 3) == 0 || strncmp(val, "profile-", 8) == 0) {
            if (!da->deferred) {
                da->deferred = s;
                da->deferred_sprint = "is out of scope for v0.1.0";
            }
            break;
        }
        if (strcmp(val, "pic") == 0 || strcmp(val, "PIC") == 0 ||
            strcmp(val, "pie") == 0 || strcmp(val, "PIE") == 0) {
            if (!da->deferred) {
                da->deferred = s;
                da->deferred_sprint = "lands in Sprint 51";
            }
            break;
        }
        VecStr_push(&da->warn_unrecognized, s);
        break;
    }
    }
    return true;
}

static bool h_deferred(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    (void)val;
    if (da->deferred)
        return true;
    switch (fs->code) {
    case F_SHARED:
        da->deferred = "-shared";
        da->deferred_sprint = "lands in Sprint 51";
        break;
    }
    return true;
}

static bool h_link(DriverArgs *da, const FlagSpec *fs, const char *val)
{
    LinkInput li;

    switch (fs->code) {
    case F_LIB:
        li.kind = LINK_LIB;
        li.val = val;
        VecLink_push(&da->link_inputs, li);
        break;
    case F_XLINKER:
        li.kind = LINK_RAW;
        li.val = val;
        VecLink_push(&da->link_inputs, li);
        break;
    case F_WL: {
        /* -Wl,a,b: comma-split, each an arg AT THAT POSITION. "-Wl," is
         * an empty split — zero args, accepted (corner-case pinned). */
        const char *p = val;

        while (*p) {
            const char *comma = strchr(p, ',');
            size_t n = comma ? (size_t)(comma - p) : strlen(p);

            li.kind = LINK_RAW;
            li.val = astr(g_ps->arena, p, n);
            VecLink_push(&da->link_inputs, li);
            if (!comma)
                break;
            p = comma + 1;
        }
        break;
    }
    case F_STATIC:
        da->static_link = true;
        break;
    case F_NOSTDLIB:
        da->nostdlib = true;
        break;
    case F_NOSTARTFILES:
        da->nostartfiles = true;
        break;
    case F_NODEFAULTLIBS:
        da->nodefaultlibs = true;
        break;
    }
    return true;
}

/* ---- THE TABLE ------------------------------------------------------ */

static const FlagSpec args_flag_table[] = {
    /* info / introspection */
    {"--help", ARG_NONE, h_info, F_HELP},
    {"--help=warnings", ARG_NONE, h_info, F_HELP_WARNINGS},
    {"--version", ARG_NONE, h_info, F_VERSION},
    {"-dumpversion", ARG_NONE, h_info, F_DUMPVERSION},
    {"-dumpmachine", ARG_NONE, h_info, F_DUMPMACHINE},
    {"-print-search-dirs", ARG_NONE, h_info, F_PRINT_SEARCH},
    {"-print-prog-name=", ARG_JOINED, h_info, F_PRINT_PROG},
    {"-print-file-name=", ARG_JOINED, h_info, F_PRINT_FILE},
    {"-v", ARG_NONE, h_info, F_VERBOSE},
    {"-###", ARG_NONE, h_info, F_DRY_RUN},
    /* modes and outputs */
    {"-E", ARG_NONE, h_mode, F_MODE_E},
    {"-S", ARG_NONE, h_mode, F_MODE_S},
    {"-c", ARG_NONE, h_mode, F_MODE_C},
    {"-o", ARG_EITHER, h_output, F_OUTPUT},
    {"-x", ARG_EITHER, h_xlang, F_XLANG},
    {"-P", ARG_NONE, h_mode, F_NO_LINEMARKERS},
    {"-dM", ARG_NONE, h_mode, F_DUMP_MACROS},
    {"--dump-tokens", ARG_NONE, h_mode, F_DUMP_TOKENS},
    {"--dump-ast", ARG_NONE, h_mode, F_DUMP_AST},
    {"-fdump-sema", ARG_NONE, h_mode, F_DUMP_SEMA},
    {"-fdump-layout", ARG_NONE, h_mode, F_DUMP_LAYOUT},
    {"-fdump-init", ARG_NONE, h_mode, F_DUMP_INIT},
    {"-fsyntax-only", ARG_NONE, h_mode, F_SYNTAX_ONLY},
    {"-emit-ir", ARG_NONE, h_mode, F_EMIT_IR},
    {"-emit-mir", ARG_NONE, h_mode, F_EMIT_MIR},
    /* language */
    {"-std=", ARG_JOINED, h_std, F_STD},
    {"-trigraphs", ARG_NONE, h_mode, F_TRIGRAPHS},
    {"-pedantic", ARG_NONE, h_warn, F_PEDANTIC},
    {"-pedantic-errors", ARG_NONE, h_warn, F_PEDANTIC_ERR},
    {"-fmax-errors=", ARG_JOINED, h_max_errors, F_MAX_ERRORS},
    {"-ferror-limit=", ARG_JOINED, h_max_errors, F_MAX_ERRORS},
    /* preprocessor */
    {"-I", ARG_EITHER, h_dir, F_DIR_I},
    {"-iquote", ARG_EITHER, h_dir, F_DIR_IQUOTE},
    {"-isystem", ARG_EITHER, h_dir, F_DIR_ISYSTEM},
    {"-include", ARG_SEPARATE, h_dir, F_INCLUDE},
    {"-D", ARG_EITHER, h_def, F_DEFINE},
    {"-U", ARG_EITHER, h_def, F_UNDEF},
    {"-nostdinc", ARG_NONE, h_mode, F_NOSTDINC},
    /* dependency generation */
    {"-M", ARG_NONE, h_dep, F_DEP_M},
    {"-MM", ARG_NONE, h_dep, F_DEP_MM},
    {"-MD", ARG_NONE, h_dep, F_DEP_MD},
    {"-MMD", ARG_NONE, h_dep, F_DEP_MMD},
    {"-MF", ARG_EITHER, h_dep, F_DEP_MF},
    {"-MT", ARG_EITHER, h_dep, F_DEP_MT},
    {"-MQ", ARG_EITHER, h_dep, F_DEP_MQ},
    {"-MP", ARG_NONE, h_dep, F_DEP_MP},
    /* optimization / warnings */
    {"-O", ARG_JOINED, h_opt, F_OPT_O},
    {"-ftime-report", ARG_NONE, h_mode, F_TIME_REPORT},
    {"-w", ARG_NONE, h_warn, F_WSUPPRESS},
    {"-Werror", ARG_NONE, h_warn, F_WERROR},
    {"-Werror=", ARG_JOINED, h_warn, F_WERROR_EQ},
    {"-W", ARG_JOINED, h_warn, F_WGENERAL},
    /* -f family */
    {"-fcommon", ARG_NONE, h_fflag, F_FCOMMON},
    {"-fno-common", ARG_NONE, h_fflag, F_FNO_COMMON},
    {"-ffreestanding", ARG_NONE, h_fflag, F_FFREESTANDING},
    {"-fhosted", ARG_NONE, h_fflag, F_FHOSTED},
    {"-fwrapv", ARG_NONE, h_fflag, F_FWRAPV},
    {"-fsafe", ARG_NONE, h_fflag, F_FSAFE},
    {"-fcgf-safe", ARG_NONE, h_fflag, F_FCGF_SAFE},
    {"-fno-cgf-safe", ARG_NONE, h_fflag, F_FNO_CGF_SAFE},
    {"-fsafe-allow-unsafe=", ARG_JOINED, h_fflag, F_FSAFE_ALLOW_UNSAFE},
    {"-fdiagnostics-parseable-fixits", ARG_NONE, h_fflag,
     F_FDIAG_PARSEABLE_FIXITS},
    {"-fdiagnostics-apply-fixits", ARG_NONE, h_fflag, F_FDIAG_APPLY_FIXITS},
    {"-fdiagnostics-apply-fixits=", ARG_JOINED, h_fflag, F_FDIAG_APPLY_FIXITS},
    {"-ftrivial-auto-var-init", ARG_NONE, h_fflag, F_FTRIVIAL_AUTO_VAR_INIT},
    {"-ftrivial-auto-var-init=", ARG_JOINED, h_fflag, F_FTRIVIAL_AUTO_VAR_INIT},
    {"-fstrict-aliasing", ARG_NONE, h_fflag, F_FSTRICT_ALIAS},
    {"-fno-strict-aliasing", ARG_NONE, h_fflag, F_FNO_STRICT_ALIAS},
    {"-fomit-frame-pointer", ARG_NONE, h_fflag, F_FOMIT_FP},
    {"-fno-omit-frame-pointer", ARG_NONE, h_fflag, F_FNO_OMIT_FP},
    {"-ffast-math", ARG_NONE, h_fflag, F_FFAST_MATH},
    {"-fno-fast-math", ARG_NONE, h_fflag, F_FNO_FAST_MATH},
    {"-fassociative-math", ARG_NONE, h_fflag, F_FASSOCIATIVE_MATH},
    {"-fno-associative-math", ARG_NONE, h_fflag, F_FNO_ASSOCIATIVE_MATH},
    {"-fsigned-zeros", ARG_NONE, h_fflag, F_FSIGNED_ZEROS},
    {"-fno-signed-zeros", ARG_NONE, h_fflag, F_FNO_SIGNED_ZEROS},
    {"-ffinite-math-only", ARG_NONE, h_fflag, F_FFINITE_MATH},
    {"-fno-finite-math-only", ARG_NONE, h_fflag, F_FNO_FINITE_MATH},
    {"-freciprocal-math", ARG_NONE, h_fflag, F_FRECIPROCAL_MATH},
    {"-fno-reciprocal-math", ARG_NONE, h_fflag, F_FNO_RECIPROCAL_MATH},
    {"-fmath-errno", ARG_NONE, h_fflag, F_FMATH_ERRNO},
    {"-fno-math-errno", ARG_NONE, h_fflag, F_FNO_MATH_ERRNO},
    {"-ffp-contract=", ARG_JOINED, h_fflag, F_FFP_CONTRACT},
    {"-f", ARG_JOINED, h_fflag, F_FGENERAL},
    /* debug / deferred */
    {"-g", ARG_JOINED, h_debug, F_DEBUG_G},
    {"-shared", ARG_NONE, h_deferred, F_SHARED},
    /* link */
    {"-L", ARG_EITHER, h_dir, F_DIR_L},
    {"-l", ARG_EITHER, h_link, F_LIB},
    {"-Wl,", ARG_JOINED, h_link, F_WL},
    {"-Xlinker", ARG_SEPARATE, h_link, F_XLINKER},
    {"-static", ARG_NONE, h_link, F_STATIC},
    {"-nostdlib", ARG_NONE, h_link, F_NOSTDLIB},
    {"-nostartfiles", ARG_NONE, h_link, F_NOSTARTFILES},
    {"-nodefaultlibs", ARG_NONE, h_link, F_NODEFAULTLIBS},
    {"-B", ARG_EITHER, h_dir, F_DIR_B},
};

/* Exact-name hit first; then the LONGEST prefix among entries that can
 * take a joined value. ARG_SEPARATE never prefix-matches ("-includefoo"
 * is unknown, exactly as gcc treats it). */
static const FlagSpec *match_flag(const char *s, const char **joined_val)
{
    const FlagSpec *best = NULL;
    size_t best_len = 0, i;

    *joined_val = NULL;
    for (i = 0; i < CGF_ARRAY_LEN(args_flag_table); i++)
        if (strcmp(args_flag_table[i].name, s) == 0)
            return &args_flag_table[i];
    for (i = 0; i < CGF_ARRAY_LEN(args_flag_table); i++) {
        const FlagSpec *fs = &args_flag_table[i];
        size_t n;

        if (fs->style != ARG_JOINED && fs->style != ARG_EITHER)
            continue;
        n = strlen(fs->name);
        if (strncmp(fs->name, s, n) == 0 && n > best_len) {
            best = fs;
            best_len = n;
        }
    }
    if (best)
        *joined_val = s + best_len;
    return best;
}

/* Did-you-mean: Damerau-Levenshtein <= 2 against every table name (the
 * same helper sema's typo suggestions use). */
static void suggest_flag(DriverArgs *da, const char *s)
{
    const char *best = NULL;
    unsigned best_d = 3;
    size_t i;

    for (i = 0; i < CGF_ARRAY_LEN(args_flag_table); i++) {
        const char *name = args_flag_table[i].name;
        unsigned d = dlev_distance(s, strlen(s), name, strlen(name), 2);

        if (d < best_d) {
            best_d = d;
            best = name;
        }
    }
    if (best)
        snprintf(da->suggest, sizeof(da->suggest), "%s", best);
}

/* ---- inputs --------------------------------------------------------- */

static InputKind kind_from_ext(const char *path)
{
    const char *dot = strrchr(path, '.');

    if (!dot || strchr(dot, '/'))
        return IN_LINK;
    if (strcmp(dot, ".c") == 0)
        return IN_C;
    if (strcmp(dot, ".i") == 0)
        return IN_CPP_OUT;
    if (strcmp(dot, ".s") == 0)
        return IN_ASM;
    if (strcmp(dot, ".S") == 0)
        return IN_ASM_PP;
    if (strcmp(dot, ".cgfir") == 0)
        return IN_CGFIR;
    return IN_LINK; /* .o/.a/anything else: link input, in position */
}

static void add_input(DriverArgs *da, const char *path)
{
    DriverInput in;
    LinkInput li;

    in.path = path;
    in.link_slot = -1;
    if (strcmp(path, "-") == 0) {
        /* stdin needs an active -x c: there is no extension to dispatch
         * on, and guessing would hide a wrong build rule. */
        if (g_ps->x_lang < 0) {
            if (!da->stdin_no_x)
                da->stdin_no_x = path;
            return;
        }
        in.kind = (u8)g_ps->x_lang;
    } else if (g_ps->x_lang >= 0) {
        in.kind = (u8)g_ps->x_lang;
    } else {
        in.kind = (u8)kind_from_ext(path);
    }
    if (in.kind == IN_LINK) {
        /* Straight to the link stream at THIS position. */
        li.kind = LINK_OBJ;
        li.val = path;
        VecLink_push(&da->link_inputs, li);
        in.link_slot = (int)da->link_inputs.len - 1;
    } else if (in.kind != IN_CGFIR) {
        /* A compiled TU's object claims its argv position in the link
         * stream now; the path is filled in after compiling. */
        li.kind = LINK_OBJ;
        li.val = NULL;
        VecLink_push(&da->link_inputs, li);
        in.link_slot = (int)da->link_inputs.len - 1;
    }
    VecInput_push(&da->inputs, in);
}

/* ---- response files ------------------------------------------------- */

#define RSP_DEPTH_MAX 16

VEC_DECL(VecArgv, char *);

static bool parse_one(DriverArgs *da, const char *s, int argc, char **argv,
                      int *i, int depth);

static bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
           c == '\f';
}

/* @file: whitespace-split, '...'/"..." group, backslash escapes the next
 * character outside quotes, no comment syntax, recursive with a depth
 * cap. An unreadable file is treated LITERALLY as an argument — gcc
 * parity, and configure fragments rely on it. The whole file is
 * tokenized FIRST so ARG_SEPARATE/ARG_EITHER flags can consume their
 * next token exactly as they would from argv. */
static void rsp_expand(DriverArgs *da, const char *arg, int depth)
{
    FILE *f;
    long len;
    char *buf;
    size_t n, i;
    VecArgv toks = {0};
    int j;

    if (depth > RSP_DEPTH_MAX) {
        if (!da->rsp_error)
            da->rsp_error = "response files nested too deeply";
        return;
    }
    f = fopen(arg + 1, "rb");
    if (!f) {
        add_input(da, arg); /* literal, per gcc */
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        add_input(da, arg);
        return;
    }
    buf = arena_alloc(g_ps->arena, (size_t)len + 1, 1);
    n = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[n] = '\0';

    i = 0;
    while (i < n) {
        char *tok;
        size_t w = 0;
        char quote = 0;

        while (i < n && is_ws(buf[i]))
            i++;
        if (i >= n)
            break;
        tok = arena_alloc(g_ps->arena, n - i + 1, 1);
        while (i < n) {
            char c = buf[i];

            if (quote) {
                if (c == quote) {
                    quote = 0;
                    i++;
                    continue;
                }
                tok[w++] = c;
                i++;
                continue;
            }
            if (c == '\'' || c == '"') {
                quote = c;
                i++;
                continue;
            }
            if (c == '\\' && i + 1 < n) {
                tok[w++] = buf[i + 1];
                i += 2;
                continue;
            }
            if (is_ws(c))
                break;
            tok[w++] = c;
            i++;
        }
        tok[w] = '\0';
        VecArgv_push(&toks, tok);
    }
    /* -1-based like argv's program-name slot is not present here, so the
     * loop starts at 0 with argc = toks.len. */
    for (j = 0; j < (int)toks.len; j++)
        parse_one(da, toks.data[j], (int)toks.len, toks.data, &j, depth);
    VecArgv_free(&toks);
}

/* ---- the parse loop ------------------------------------------------- */

/* Handles ONE argument (which may consume the next argv for separate
 * values). depth carries response-file nesting. */
static bool parse_one(DriverArgs *da, const char *s, int argc, char **argv,
                      int *i, int depth)
{
    const FlagSpec *fs;
    const char *joined;
    const char *val = NULL;

    if (s[0] == '@' && s[1] != '\0') {
        rsp_expand(da, s, depth + 1);
        return true;
    }
    if (s[0] != '-' || s[1] == '\0') {
        /* A file, or the bare "-" stdin marker. */
        add_input(da, s);
        return true;
    }
    fs = match_flag(s, &joined);
    if (!fs) {
        if (!da->unknown_opt) {
            da->unknown_opt = s;
            suggest_flag(da, s);
        }
        return true;
    }
    switch (fs->style) {
    case ARG_NONE:
        /* Exact matches only ever land here (prefix scan skips
         * ARG_NONE), so there is no stray joined text. */
        val = NULL;
        break;
    case ARG_JOINED:
        val = joined ? joined : "";
        break;
    case ARG_SEPARATE:
        if (argv && *i + 1 < argc) {
            val = argv[++*i];
        } else {
            if (!da->missing_arg)
                da->missing_arg = fs->name;
            return true;
        }
        break;
    case ARG_EITHER:
        if (joined && joined[0] != '\0') {
            val = joined;
        } else if (argv && *i + 1 < argc) {
            /* Consumes the NEXT argv even if it looks like a flag:
             * `-o -v` names the output "-v" — gcc does too. */
            val = argv[++*i];
        } else {
            if (!da->missing_arg)
                da->missing_arg = fs->name;
            return true;
        }
        break;
    }
    return fs->handle(da, fs, val);
}

DriverArgs args_parse(struct Arena *arena, int argc, char **argv)
{
    DriverArgs a;
    ParseState ps;
    int i;

    memset(&a, 0, sizeof(a));
    a.std = 3;           /* STD_C17 — the locked default */
    a.fno_common = true; /* gcc >= 10 semantics (documented divergence) */
    ps.arena = arena;
    ps.x_lang = -1;
    g_ps = &ps;

    for (i = 1; i < argc; i++)
        parse_one(&a, argv[i], argc, argv, &i, 0);
    g_ps = NULL;

    /* -M/-MM imply -E: depfile INSTEAD of compilation (gcc parity). */
    if (a.dep_mode != DEP_OFF && !a.dep_side)
        a.mode_E = true;

    /* -fsafe is a policy profile, not a spelling alias whose pieces may
     * be weakened by a later option. Compose it after the whole argv. */
    if (a.fsafe) {
        WarnOpt mem_errors = {"error=mem"};
        WarnOpt uninit_errors = {"error=uninitialized"};

        if (a.fcgf_safe_disabled)
            a.fsafe_conflict = true;
        if (a.no_warnings)
            a.fsafe_warning_conflict = true;
        a.fcgf_safe = true;
        a.trivial_auto_var_init = AUTO_VAR_INIT_ZERO;
        VecWarn_push(&a.warn_opts, mem_errors);
        VecWarn_push(&a.warn_opts, uninit_errors);
    }

    /* -o is forbidden with multiple inputs under -c/-S/-E: each input
     * names its own output, so one -o cannot serve. */
    if (a.output && (a.compile_obj || a.emit_asm || a.mode_E)) {
        size_t compiled = 0, k;

        for (k = 0; k < a.inputs.len; k++)
            if (a.inputs.data[k].kind != IN_LINK)
                compiled++;
        if (compiled > 1)
            a.o_multi_conflict = true;
    }
    return a;
}

void args_free(DriverArgs *a)
{
    VecWarn_free(&a->warn_opts);
    VecStr_free(&a->include_dirs);
    VecStr_free(&a->iquote_dirs);
    VecStr_free(&a->isystem_dirs);
    VecStr_free(&a->pre_includes);
    VecDef_free(&a->defs);
    VecStr_free(&a->dep_targets);
    VecStr_free(&a->lib_dirs);
    VecStr_free(&a->prefix_dirs);
    VecStr_free(&a->fsafe_allow_unsafe);
    VecLink_free(&a->link_inputs);
    VecInput_free(&a->inputs);
    VecStr_free(&a->warn_unrecognized);
    VecStr_free(&a->warn_unknown_negative);
    VecStr_free(&a->warn_fast_math);
}
