/*
 * CAMP-OPENSSL-001: audited replacement for OpenSSL's compile-only
 * _Complex/header-coexistence probe.  The upstream source explicitly has no
 * runtime checks; the campaign retains its successful executable recipe while
 * _Complex remains outside Cgfried v0.1.0.
 */
#include <stdlib.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return EXIT_SUCCESS;
}
