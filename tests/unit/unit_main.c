#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "unit.h"

void t_fail(TestCtx *t, const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    t->failures++;
    fprintf(stderr, "FAIL %s at %s:%d: ", t->name, file, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

int main(int argc, char **argv)
{
    const char *filter = NULL;
    int list_only = 0;
    int failed_tests = 0, ran = 0, assertions = 0;
    size_t i;

    for (i = 1; (int)i < argc; i++) {
        if (strcmp(argv[i], "--filter") == 0 && (int)(i + 1) < argc)
            filter = argv[++i];
        else if (strcmp(argv[i], "--list") == 0)
            list_only = 1;
        else {
            fprintf(stderr, "usage: unit_tests [--list] [--filter <substr>]\n");
            return 1;
        }
    }

    if (list_only) {
        for (i = 0; i < cgf_unit_test_count; i++)
            printf("%s\n", cgf_unit_tests[i].name);
        printf("%zu tests\n", cgf_unit_test_count);
        return 0;
    }

    for (i = 0; i < cgf_unit_test_count; i++) {
        TestCtx t;

        if (filter && !strstr(cgf_unit_tests[i].name, filter))
            continue;
        t.name = cgf_unit_tests[i].name;
        t.failures = 0;
        t.assertions = 0;
        cgf_unit_tests[i].fn(&t);
        ran++;
        assertions += t.assertions;
        if (t.failures) {
            failed_tests++;
            printf("FAIL %s\n", t.name);
        } else {
            printf("PASS %s\n", t.name);
        }
    }

    printf("unit: %d tests, %d assertions, %d failures\n", ran, assertions,
           failed_tests);
    return failed_tests ? 1 : 0;
}
