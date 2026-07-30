#include "driver/driver.h"

#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "driver/toolchain.h"
#include "pp/pp.h"
#include "target.h"
#include "util/arena.h"
#include "util/intern.h"

/* One string, hand-organized — no generated help (locked style). Info-option
 * precedence as implemented: --help over --version over -dumpversion. */
static const char help_text[] =
    "Usage: cgfried [options] file...\n"
    "\n"
    "Modes:\n"
    "  -E                preprocess only, print tokens to stdout\n"
    "  -P                omit linemarkers from -E output\n"
    "  (full compilation is not yet supported; the pipeline is under\n"
    "  construction sprint by sprint)\n"
    "\n"
    "Language:\n"
    "  -trigraphs        enable trigraph translation (default off)\n"
    "  -D name[=value]   predefine a macro (value defaults to 1)\n"
    "  -U name           undefine a macro (processed in -D/-U order)\n"
    "\n"
    "Directories:\n"
    "  -I dir            add dir to the include search path\n"
    "  -iquote dir       add dir to the \"...\"-form-only search path\n"
    "  -nostdinc         do not search the system include directories\n"
    "  -v                print the include search list to stderr\n"
    "\n"
    "Output:\n"
    "  --help            print this help and exit\n"
    "  --version         print version and host target, then exit\n"
    "  -dumpversion      print the bare version number, then exit\n"
    "\n"
    "Diagnostics:\n"
    "  Diagnostics are colored when stderr is a terminal. If multiple info\n"
    "  options are given, --help wins over --version over -dumpversion.\n"
    "\n"
    "Environment:\n"
    "  NO_COLOR          disable diagnostic colors (any non-empty value)\n"
    "  CLICOLOR_FORCE    force diagnostic colors even when piped\n"
    "  CGF_AS            unset/1: use bundled afs-as (default); 0: system "
    "'as'\n"
    "  CGF_AS_PATH       use exactly this assembler (wins over CGF_AS)\n"
    "  CGF_LD            unset/0: use system 'ld' (default); 1: afs-ld "
    "(Sprint 27)\n"
    "  CGF_LD_PATH       use exactly this linker (wins over CGF_LD)\n"
    "  CGF_CRT_DIR       crt object discovery override (used from Sprint "
    "27)\n"
    "  CGF_PP_DUMP_TOKENS  with -E: dump one token per line (testing)\n"
    "  CGF_PP_DUMP_GUARD   with -E: dump include-guard shapes (testing)\n"
    "  Empty-string values are treated as unset.\n";

/* Builds the "<command-line>" pseudo-file from -D/-U flags, in order.
 * -D name means 1; -D name=val splits at the first '='. */
static SourceFile *build_cmdline_file(Preprocessor *pp, const DriverArgs *a)
{
    Buf b;
    SourceFile *sf;
    int i;

    if (a->n_defs == 0)
        return NULL;
    buf_init(&b);
    for (i = 0; i < a->n_defs; i++) {
        const char *d = a->defs[i];
        const char *eq = strchr(d, '=');

        if (a->def_is_undef[i]) {
            buf_printf(&b, "#undef %s\n", d);
        } else if (eq) {
            buf_printf(&b, "#define %.*s %s\n", (int)(eq - d), d, eq + 1);
        } else {
            buf_printf(&b, "#define %s 1\n", d);
        }
    }
    sf =
        pp_source_add_buffer(pp, "<command-line>", (const char *)b.data, b.len);
    buf_free(&b);
    return sf;
}

/* -E: preprocess through the directive engine, printed with SPACE/BOL-
 * faithful spacing (exact line-count fidelity is Sprint 6's problem). */
static int run_preprocess(Arena *arena, DiagCtx *dc, const DriverArgs *a)
{
    Interner interner;
    Preprocessor pp;
    SourceFile *sf, *cmdline;
    PpToken t;
    PpToken prev_tok;
    bool dump = cgf_env("CGF_PP_DUMP_TOKENS") != NULL;
    bool dump_guard = cgf_env("CGF_PP_DUMP_GUARD") != NULL;
    bool first = true;

    memset(&prev_tok, 0, sizeof(prev_tok));
    int i;

    intern_init(&interner, arena);
    pp_init(&pp, arena, dc, &interner);
    pp.trigraphs = a->trigraphs;
    pp.verbose = a->verbose;
    pp.std = (CStd)a->std;
    (void)a->no_linemarkers; /* -P: we never emit linemarkers (Sprint 7
                                owns line fidelity); accepted so oracle
                                command lines stay symmetric */
    pp.gnu_mode = pp.std >= STD_GNU89;
    for (i = 0; i < a->n_include; i++)
        pp.include_dirs[pp.n_include++] = a->include_dirs[i];
    for (i = 0; i < a->n_iquote; i++)
        pp.iquote_dirs[pp.n_iquote++] = a->iquote_dirs[i];
    if (!a->nostdinc) {
        /* Our shipped freestanding headers (stddef.h et al.) come FIRST —
         * glibc's own headers include them and gcc likewise places its own
         * include dir ahead of /usr/include. Dev tree: build/../include;
         * installed: <prefix>/bin/../include. */
        static char shipped[4096];
        if (cgf_exe_relative("/../include", shipped, sizeof(shipped)))
            pp.system_dirs[pp.n_system++] = shipped;
        pp.n_system += cgf_target_system_include_dirs(
            cgf_target_host(), pp.system_dirs + pp.n_system,
            PP_MAX_DIRS - pp.n_system);
    }

    sf = pp_source_load(&pp, a->input);
    if (!sf) {
        intern_free(&interner);
        pp_loc_free(&pp.loc);
        strmap_free(&pp.macros);
        return CGF_EXIT_IO;
    }
    cmdline = build_cmdline_file(&pp, a);
    pp_begin(&pp, sf, cmdline);

    while (pp_next(&pp, &t)) {
        if (dump) {
            FileId f;
            u32 line, col;

            pp_loc_resolve(&pp.loc, t.loc, &f, &line, &col);
            printf("%s %s %s:%u:%u%s%s\n", pp_tok_kind_name((PpTokKind)t.kind),
                   t.spelling, pp.files[f - 1]->path, (unsigned)line,
                   (unsigned)col, (t.flags & PPTOK_F_BOL) ? " BOL" : "",
                   (t.flags & PPTOK_F_SPACE) ? " SPACE" : "");
        } else {
            if (!first && (t.flags & PPTOK_F_BOL)) {
                putchar('\n');
            } else if (!first) {
                /* Explicit spacing, or gcc's avoid-paste rule: adjacent
                 * tokens that would re-lex as one must stay two. */
                if ((t.flags & PPTOK_F_SPACE) ||
                    pp_tokens_would_merge(&pp, &prev_tok, &t))
                    putchar(' ');
            }
            fputs(t.spelling, stdout);
            prev_tok = t;
            first = false;
        }
    }
    if (!dump && !first)
        putchar('\n');
    if (a->dump_macros)
        pp_dump_macros(&pp);
    if (dump_guard) {
        /* Include-guard shape probe (Sprint 6 detector; Sprint 7 fast
         * path consumes guard_macro). One line per file, in load order. */
        size_t fi;
        for (fi = 0; fi < pp.nfiles; fi++)
            printf("GUARD %s %s\n", pp.files[fi]->path,
                   pp.files[fi]->guard_macro ? pp.files[fi]->guard_macro : "-");
    }

    pp_end(&pp);
    intern_free(&interner);
    pp_loc_free(&pp.loc);
    strmap_free(&pp.macros);
    return diag_had_error(dc) ? CGF_EXIT_COMPILE : CGF_EXIT_OK;
}

int driver_main(int argc, char **argv)
{
    static const Span no_span = {0};
    Arena arena;
    DiagCtx *dc;
    DriverArgs a;
    int status = CGF_EXIT_OK;

    if (argc < 2) {
        fprintf(stderr, "usage: cgfried [options] file...\n");
        fprintf(stderr, "try 'cgfried --help' for the option list\n");
        return CGF_EXIT_COMPILE;
    }

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    a = args_parse(argc, argv);

    if (a.unknown_opt) {
        diag_emit(dc, DIAG_ERROR, no_span, "unknown option '%s'",
                  a.unknown_opt);
        status = CGF_EXIT_COMPILE;
    } else if (a.missing_arg) {
        diag_emit(dc, DIAG_ERROR, no_span, "option '%s' requires an argument",
                  a.missing_arg);
        status = CGF_EXIT_COMPILE;
    } else if (a.too_many) {
        diag_emit(dc, DIAG_ERROR, no_span,
                  "too many '%s' options (fixed cap until Sprint 26)",
                  a.too_many);
        status = CGF_EXIT_COMPILE;
    } else if (a.show_help) {
        fputs(help_text, stdout);
    } else if (a.show_version) {
        printf("cgfried %s (%s)\n", CGF_VERSION,
               cgf_target_name(cgf_target_host()));
    } else if (a.show_dumpversion) {
        printf("%s\n", CGF_VERSION);
    } else if (a.extra_input) {
        diag_emit(dc, DIAG_ERROR, no_span,
                  "only one input file is supported for now ('%s' and '%s' "
                  "given)",
                  a.input, a.extra_input);
        status = CGF_EXIT_COMPILE;
    } else if (a.input) {
        cgf_ice_set_input(a.input);
        if (a.mode_E) {
            status = run_preprocess(&arena, dc, &a);
        } else {
            diag_emit(dc, DIAG_ERROR, no_span,
                      "only -E (preprocessing) is supported so far: full "
                      "compilation lands sprint by sprint (next: Sprint 5, "
                      "macro expansion)");
            status = CGF_EXIT_COMPILE;
        }
    } else {
        /* Options only, none of them info options, nothing to do. */
        fprintf(stderr, "cgfried: no input files\n");
        status = CGF_EXIT_COMPILE;
    }

    arena_free_all(&arena);
    return status;
}
