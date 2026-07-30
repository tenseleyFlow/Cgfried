#include "driver/driver.h"

#include <stdio.h>

#include "diag.h"
#include "target.h"
#include "util/arena.h"

/* One string, hand-organized — no generated help (locked style). Info-option
 * precedence as implemented: --help over --version over -dumpversion. */
static const char help_text[] =
    "Usage: cgfried [options] file...\n"
    "\n"
    "Modes:\n"
    "  (compilation of C sources is not yet supported; it lands in the\n"
    "  preprocessor sprint, Sprint 3)\n"
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
    "  Empty-string values are treated as unset.\n";

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
    } else if (a.input) {
        cgf_ice_set_input(a.input);
        diag_emit(dc, DIAG_ERROR, no_span,
                  "compilation is not yet supported: Sprint 3 (preprocessor)");
        status = CGF_EXIT_COMPILE;
    } else {
        /* Options only, none of them info options, nothing to do. */
        fprintf(stderr, "cgfried: no input files\n");
        status = CGF_EXIT_COMPILE;
    }

    arena_free_all(&arena);
    return status;
}
