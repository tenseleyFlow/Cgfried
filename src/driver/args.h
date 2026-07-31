#ifndef CGF_DRIVER_ARGS_H
#define CGF_DRIVER_ARGS_H

#include <stdbool.h>

#include "util/base.h"
#include "util/vec.h"

struct Arena;

/* Sprint 26: the gcc-compatible flag surface. args_parse is table-driven
 * (FlagSpec in args.c — zero strcmp dispatch chains outside it) and does
 * no I/O except response-file reads, so it unit-tests pure. Everything
 * here is parsed and STORED this sprint; some semantics route elsewhere:
 * -W machinery is Sprint 37, -g is Sprint 29, -shared/-fPIC Sprint 51,
 * crt/archive link depth Sprint 27. */

/* Input dispatch: extension decides unless an active -x overrides. */
typedef enum {
    IN_C,       /* .c (or -x c): pp + compile + assemble */
    IN_CPP_OUT, /* .i: already preprocessed; compile without cmdline PP */
    IN_ASM,     /* .s: assemble only */
    IN_ASM_PP,  /* .S: preprocess, then assemble */
    IN_CGFIR,   /* .cgfir: IR round-trip path (cgf extension) */
    IN_LINK,    /* .o/.a/anything else: link input, kept in position */
} InputKind;

typedef struct {
    const char *path; /* "-" = stdin (legal only under an active -x c) */
    u8 kind;          /* InputKind */
    int link_slot;    /* index into link_inputs this input's object will
                         fill after compiling; -1 when the mode produces
                         no link input (-E/-S/-c and non-linking dumps) */
} DriverInput;

/* THE position-sensitive link stream: objects, -l libs and -Wl,/-Xlinker
 * raw args in exact command-line order. The driver must never reorder —
 * `cgf -lm main.o` has to fail exactly like gcc's (archive-position
 * semantics, Sprint 27). -L dirs are hoisted separately because GNU ld
 * applies all -L to all -l regardless of position. */
typedef enum { LINK_OBJ, LINK_LIB, LINK_RAW } LinkKind;
typedef struct LinkInput {
    u8 kind;         /* LinkKind */
    const char *val; /* LINK_OBJ owned by a compiled TU starts NULL and is
                        filled with the produced object path post-compile */
} LinkInput;

/* -W options in command-line order; Sprint 37 interprets. `name` is the
 * spelling after "-W" ("all", "no-shadow", "error=format"). */
typedef struct {
    const char *name;
} WarnOpt;

typedef enum { DEP_OFF, DEP_M, DEP_MM } DepMode;

/* Optimization levels, last one wins. Pipelines are Phase 7; only the
 * stored level exists this sprint. */
typedef enum { OPT_O0, OPT_O1, OPT_O2, OPT_O3, OPT_OS, OPT_OFAST } OptLevel;

typedef struct {
    const char *val; /* "NAME", "NAME=VAL" (-D) or "NAME" (-U) */
    bool is_undef;
} CmdDefine; /* named to dodge pp.h's MacroDef */

VEC_DECL(VecInput, DriverInput);
VEC_DECL(VecLink, LinkInput);
VEC_DECL(VecWarn, WarnOpt);
VEC_DECL(VecStr, const char *);
VEC_DECL(VecDef, CmdDefine);

typedef struct {
    /* --- info / introspection (config scripts probe these exactly) --- */
    bool show_version;     /* --version: "cgfried 0.1.0 (<target>)" */
    bool show_dumpversion; /* -dumpversion: "0.1.0" */
    bool show_dumpmachine; /* -dumpmachine: cgf_target_name() */
    bool show_help;
    bool print_search_dirs; /* -print-search-dirs */
    const char *print_prog; /* -print-prog-name=X, or NULL */
    const char *print_file; /* -print-file-name=X, or NULL */
    bool verbose;           /* -v: print each subcommand before running */
    bool dry_run;           /* -###: print subcommands, run NOTHING */

    /* --- modes --- */
    bool mode_E;         /* -E: preprocess only; default stdout, -o redirects */
    bool emit_asm;       /* -S: stop after codegen, <base>.s per input */
    bool compile_obj;    /* -c: stop after assemble, one .o per input (cwd) */
    bool link_exe;       /* set by the driver when no stop-mode is given */
    bool syntax_only;    /* -fsyntax-only */
    bool dump_macros;    /* -dM (with -E) */
    bool no_linemarkers; /* -P */
    bool dump_tokens, dump_ast, dump_sema, dump_layout, dump_init;
    bool emit_ir, emit_mir;
    const char *output; /* -o; consumes the next argv even if flag-like */

    /* --- language / optimization --- */
    int opt_level; /* OptLevel; -O last-one-wins; pipelines are Phase 7 */
    int std;       /* CStd value; default C17 (locked; gcc defaults gnu17
                      — the divergence is documented in --help) */
    bool trigraphs;
    bool pedantic, pedantic_errors;
    u32 max_errors; /* -fmax-errors=N / -ferror-limit=N; 0 = unlimited */

    /* --- -f flags landed so far (unknown -f warns, never errors) --- */
    bool fno_common;   /* DEFAULT TRUE: gcc >= 10 semantics; divergence
                          from the gcc 8 parity baseline is deliberate
                          and documented (sprint 26 locked decision) */
    bool freestanding; /* -ffreestanding (__STDC_HOSTED__, main rules) */
    bool fwrapv;
    bool fno_strict_aliasing; /* Sprint 32 consumes */

    /* --- warnings: parsed/stored now, interpreted in Sprint 37 --- */
    VecWarn warn_opts;
    bool werror;      /* -Werror (bare) — live for existing diagnostics */
    bool no_warnings; /* -w — live for existing diagnostics */

    /* --- preprocessor --- */
    VecStr include_dirs; /* -I */
    VecStr iquote_dirs;  /* -iquote */
    VecStr isystem_dirs; /* -isystem: after -I, before builtin dirs */
    VecStr pre_includes; /* -include, in flag order */
    VecDef defs;         /* -D/-U, strictly left-to-right */
    bool nostdinc;

    /* --- the -M dependency family --- */
    int dep_mode;         /* DepMode; -M/-MM imply -E (no compilation) */
    bool dep_side;        /* -MD/-MMD: depfile beside compiling */
    bool dep_phony;       /* -MP */
    const char *dep_file; /* -MF */
    VecStr dep_targets;   /* -MT verbatim / -MQ pre-quoted at parse time */

    /* --- link --- */
    VecStr lib_dirs;    /* -L, in order */
    VecStr prefix_dirs; /* -B, stored (tool search; Sprint 27 deepens) */
    VecLink link_inputs;
    bool static_link, nostdlib, nostartfiles, nodefaultlibs;

    /* --- inputs --- */
    VecInput inputs;

    /* --- parse outcome (driver_main renders the diagnostics) --- */
    const char *unknown_opt;     /* first unknown non--f/-W option, or NULL */
    char suggest[64];            /* "; did you mean '-o'?" payload, or "" */
    const char *missing_arg;     /* option lacking its argument, or NULL */
    const char *bad_value;       /* option whose value did not parse */
    const char *deferred;        /* flag hard-erroring until a named sprint */
    const char *deferred_sprint; /* "Sprint 29" etc, paired with above */
    const char *rsp_error;       /* response-file nesting error, or NULL */
    const char *stdin_no_x;      /* "-" was given without an active -x c */
    bool o_multi_conflict;       /* -o with -c/-S/-E and multiple inputs */
    /* -f<unknown>/-fomit-frame-pointer: warn and continue (gcc parity —
     * hard-erroring breaks flag-probing configure scripts). */
    VecStr warn_unrecognized;
} DriverArgs;

/* Pure except @file reads. The arena backs response-file contents and any
 * strings built during parsing; vec storage is heap — args_free releases
 * it. */
DriverArgs args_parse(struct Arena *arena, int argc, char **argv);
void args_free(DriverArgs *a);

/* Make-quoting for -MQ targets and written prerequisite paths:
 * $ -> $$, space -> "\ " (any run of preceding backslashes doubled),
 * # -> \#. Returns s unchanged when nothing needs quoting. */
const char *cgf_make_quote(struct Arena *arena, const char *s);

#endif
