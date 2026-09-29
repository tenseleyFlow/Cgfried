// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: atomic 'vector_size' types are not yet supported
/* Atomic vector lowering is a separate contract from the named-call ABI. */
typedef unsigned __int128 __attribute__((vector_size(16))) V;

_Atomic(V) value;
