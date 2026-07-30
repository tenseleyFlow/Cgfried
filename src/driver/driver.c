#include "driver/driver.h"

#include <stdio.h>

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
    "  (full compilation is not yet supported; the pipeline is under\n"
    "  construction sprint by sprint)\n"
    "\n"
    "Language:\n"
    "  -trigraphs        enable trigraph translation (default off)\n"
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
    "  Empty-string values are treated as unset.\n";

/* -E: translation phases 1-3, printed with SPACE/BOL-faithful spacing
 * (exact line-count fidelity is Sprint 6's problem). A directive `#`
 * hard-errors: directive processing lands in Sprint 4. */
static int run_preprocess(Arena *arena, DiagCtx *dc, const DriverArgs *a)
{
    Interner interner;
    Preprocessor pp;
    SourceFile *sf;
    PpLexer lx;
    PpToken t;
    bool dump = cgf_env("CGF_PP_DUMP_TOKENS") != NULL;
    bool first = true;

    intern_init(&interner, arena);
    pp_init(&pp, arena, dc, &interner);
    pp.trigraphs = a->trigraphs;

    sf = pp_source_load(&pp, a->input);
    if (!sf) {
        intern_free(&interner);
        pp_loc_free(&pp.loc);
        return CGF_EXIT_IO;
    }
    pp_lexer_init(&lx, &pp, sf);

    while (pp_lex_token(&lx, &t)) {
        if (t.kind == PPTOK_PUNCT && t.punct == PUNCT_HASH &&
            (t.flags & PPTOK_F_BOL)) {
            pp_diag_at(&pp, DIAG_ERROR, t.loc, t.len,
                       "preprocessor directives land in Sprint 4");
            break;
        }
        if (dump) {
            FileId f;
            u32 line, col;

            pp_loc_resolve(&pp.loc, t.loc, &f, &line, &col);
            printf("%s %s %s:%u:%u%s%s\n", pp_tok_kind_name((PpTokKind)t.kind),
                   t.spelling, pp.files[f - 1]->path, (unsigned)line,
                   (unsigned)col, (t.flags & PPTOK_F_BOL) ? " BOL" : "",
                   (t.flags & PPTOK_F_SPACE) ? " SPACE" : "");
        } else {
            if (!first && (t.flags & PPTOK_F_BOL))
                putchar('\n');
            else if (!first && (t.flags & PPTOK_F_SPACE))
                putchar(' ');
            fputs(t.spelling, stdout);
            first = false;
        }
    }
    if (!dump && !first)
        putchar('\n');

    buf_free(&lx.scratch);
    intern_free(&interner);
    pp_loc_free(&pp.loc);
    return diag_had_error(dc) ? CGF_EXIT_COMPILE : CGF_EXIT_OK;
}

int driver_main(int argc, char **argv)
{
    static const Span no_span = {0, 0, 0, 0};
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
                      "compilation lands sprint by sprint (next: Sprint 4, "
                      "directives)");
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
