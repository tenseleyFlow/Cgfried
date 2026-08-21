// RESOLVED(audit): PP-M-04 pre-expanded arguments lose their inner macro backtrace
// Reproducer: cgfried -fsyntax-only this-file.c must diagnose 09 and retain
// the ARG_BAD expansion provenance; baseline reports only the outer PASS.
#define ARG_BAD 09
#define PASS(x) x
int preexpanded_argument = PASS(ARG_BAD);
