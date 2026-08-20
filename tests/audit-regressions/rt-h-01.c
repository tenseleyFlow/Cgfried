// XFAIL(audit): RT-H-01 arm64-macos publishes the wrong long-double size macro
/* Sprint 60 F10: arm64-macos makes long double identical to double, so the
 * implementation-defined size macro must agree with the type's target ABI.
 *
 * Reproduce:
 *   build/cgfried --target=arm64-macos -fsyntax-only \
 *       tests/audit-regressions/rt-h-01.c
 *
 * Expected until Sprint 61: the first assertion passes and the second fails
 * because sizeof(long double) is 8 but __SIZEOF_LONG_DOUBLE__ is 16. */
#if !defined(__APPLE__) || !defined(__aarch64__)
#error this regression requires --target=arm64-macos
#endif

_Static_assert(sizeof(long double) == 8,
               "arm64-macos long double must follow the 8-byte Apple ABI");
_Static_assert(__SIZEOF_LONG_DOUBLE__ == sizeof(long double),
               "the predefined size macro must describe the target type");
