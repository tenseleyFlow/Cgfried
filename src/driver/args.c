#include <string.h>

#include "driver/driver.h"

/* Joined-or-separate option value: "-Idir" or "-I dir". */
static const char *opt_value(const char *s, size_t optlen, int argc,
                             char **argv, int *i)
{
    if (s[optlen] != '\0')
        return s + optlen;
    if (*i + 1 < argc)
        return argv[++*i];
    return NULL;
}

DriverArgs args_parse(int argc, char **argv)
{
    DriverArgs a;
    int i;

    memset(&a, 0, sizeof(a));
    a.std = 3; /* STD_C17 — the locked default */

    for (i = 1; i < argc; i++) {
        const char *s = argv[i];

        if (strcmp(s, "--version") == 0) {
            a.show_version = true;
        } else if (strcmp(s, "-dumpversion") == 0) {
            a.show_dumpversion = true;
        } else if (strcmp(s, "--help") == 0) {
            a.show_help = true;
        } else if (strcmp(s, "-E") == 0) {
            a.mode_E = true;
        } else if (strcmp(s, "-dM") == 0) {
            a.dump_macros = true;
        } else if (strncmp(s, "-std=", 5) == 0) {
            const char *v = s + 5;
            if (strcmp(v, "c89") == 0 || strcmp(v, "c90") == 0 ||
                strcmp(v, "iso9899:1990") == 0)
                a.std = 0; /* STD_C89 */
            else if (strcmp(v, "c99") == 0 || strcmp(v, "iso9899:1999") == 0)
                a.std = 1; /* STD_C99 */
            else if (strcmp(v, "c11") == 0 || strcmp(v, "iso9899:2011") == 0)
                a.std = 2; /* STD_C11 */
            else if (strcmp(v, "c17") == 0 || strcmp(v, "c18") == 0 ||
                     strcmp(v, "iso9899:2017") == 0)
                a.std = 3; /* STD_C17 */
            else if (strcmp(v, "gnu89") == 0 || strcmp(v, "gnu90") == 0)
                a.std = 4; /* STD_GNU89 */
            else if (strcmp(v, "gnu99") == 0)
                a.std = 5; /* STD_GNU99 */
            else if (strcmp(v, "gnu11") == 0)
                a.std = 6; /* STD_GNU11 */
            else if (strcmp(v, "gnu17") == 0 || strcmp(v, "gnu18") == 0)
                a.std = 7; /* STD_GNU17 */
            else if (!a.unknown_opt)
                a.unknown_opt = s;
        } else if (strcmp(s, "-trigraphs") == 0) {
            a.trigraphs = true;
        } else if (strcmp(s, "-nostdinc") == 0) {
            a.nostdinc = true;
        } else if (strcmp(s, "-v") == 0) {
            a.verbose = true;
        } else if (strncmp(s, "-iquote", 7) == 0) {
            const char *v = opt_value(s, 7, argc, argv, &i);
            if (!v)
                a.missing_arg = "-iquote";
            else if (a.n_iquote >= DRIVER_MAX_DIRS)
                a.too_many = "-iquote";
            else
                a.iquote_dirs[a.n_iquote++] = v;
        } else if (strncmp(s, "-I", 2) == 0) {
            const char *v = opt_value(s, 2, argc, argv, &i);
            if (!v)
                a.missing_arg = "-I";
            else if (a.n_include >= DRIVER_MAX_DIRS)
                a.too_many = "-I";
            else
                a.include_dirs[a.n_include++] = v;
        } else if (strncmp(s, "-D", 2) == 0 || strncmp(s, "-U", 2) == 0) {
            bool undef = s[1] == 'U';
            const char *v = opt_value(s, 2, argc, argv, &i);
            if (!v)
                a.missing_arg = undef ? "-U" : "-D";
            else if (a.n_defs >= DRIVER_MAX_DEFS)
                a.too_many = undef ? "-U" : "-D";
            else {
                a.defs[a.n_defs] = v;
                a.def_is_undef[a.n_defs] = undef;
                a.n_defs++;
            }
        } else if (s[0] == '-' && s[1] != '\0') {
            if (!a.unknown_opt)
                a.unknown_opt = s;
        } else {
            /* A bare "-" is stdin input, not an option. */
            if (!a.input)
                a.input = s;
            else if (!a.extra_input)
                a.extra_input = s;
        }
    }
    return a;
}
