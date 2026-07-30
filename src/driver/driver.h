#ifndef CGF_DRIVER_H
#define CGF_DRIVER_H

#include <stdbool.h>

/* Single source of truth for the version; Sprint 62 audits every copy. */
#define CGF_VERSION "0.1.0"

/* Exit-code contract (see .docs/sprints/00-scaffolding/s00). main() is the
 * single return-to-OS site; everything else returns status up. Only cgf_ice
 * may call exit — scattered exits make this table unauditable. */
enum {
    CGF_EXIT_OK = 0,      /* success */
    CGF_EXIT_COMPILE = 1, /* any diagnostic-level error, bad flags */
    CGF_EXIT_LINK = 2,    /* tool subprocess failures (Sprint 2+) */
    CGF_EXIT_IO = 3,      /* unreadable input, unwritable output */
    CGF_EXIT_ICE = 4,     /* internal compiler error, via cgf_ice */
};

/* Parsed command line. Parsing does no I/O so it unit-tests pure. Fixed
 * caps until Sprint 26's table-driven parser; overflow is a loud error. */
#define DRIVER_MAX_DIRS 32
#define DRIVER_MAX_DEFS 64

typedef struct {
    bool show_version;
    bool show_dumpversion;
    bool show_help;
    bool mode_E;             /* -E: stop after preprocessing */
    bool dump_macros;        /* -dM (requires -E) */
    bool no_linemarkers;     /* -P */
    int std;                 /* CStd value; default C17 */
    bool trigraphs;          /* -trigraphs (default off, gcc parity) */
    bool nostdinc;           /* -nostdinc: no system include dirs */
    bool verbose;            /* -v: print include search lists */
    const char *unknown_opt; /* first unrecognized option, or NULL */
    const char *missing_arg; /* option lacking its argument, or NULL */
    const char *too_many;    /* which fixed cap overflowed, or NULL */
    const char *input;       /* first input-file argument, or NULL */
    const char *extra_input; /* second input file (unsupported), or NULL */

    const char *include_dirs[DRIVER_MAX_DIRS]; /* -I, in order */
    int n_include;
    const char *iquote_dirs[DRIVER_MAX_DIRS]; /* -iquote, in order */
    int n_iquote;
    /* -D name[=val] / -U name, strictly in command-line order. */
    const char *defs[DRIVER_MAX_DEFS];
    bool def_is_undef[DRIVER_MAX_DEFS];
    int n_defs;
} DriverArgs;

DriverArgs args_parse(int argc, char **argv);
int driver_main(int argc, char **argv);

#endif
