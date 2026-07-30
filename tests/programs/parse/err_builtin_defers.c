// FLAGS: --dump-ast
// ERROR_EXPECTED: Sprint 28
// `__builtin_va_list` is a KEYWORD in the Sprint 8 table, so it never
// reaches the typedef-name path and used to produce a bare "expected a
// declarator". Our own shipped <stdarg.h> is the first thing to hit it —
// 62 of the c-testsuite programs land here — so the deferral must name
// the sprint that makes builtins real.
typedef __builtin_va_list my_va_list;
