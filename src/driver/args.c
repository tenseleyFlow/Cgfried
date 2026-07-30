#include <string.h>

#include "driver/driver.h"

DriverArgs args_parse(int argc, char **argv)
{
    DriverArgs a;

    memset(&a, 0, sizeof(a));
    int i;

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
        } else if (strcmp(s, "-trigraphs") == 0) {
            a.trigraphs = true;
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
