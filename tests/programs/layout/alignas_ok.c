// FLAGS: -fsyntax-only
// _Alignas may RAISE an alignment, never weaken it; equal is a no-op and
// zero is ignored outright (6.7.5p6) rather than being an error. The
// type-name form takes the type's own alignment.
_Alignas(16) int a;
_Alignas(4) int b;
_Alignas(0) int c;
_Alignas(double) char d;
_Alignas(long double) char e;
