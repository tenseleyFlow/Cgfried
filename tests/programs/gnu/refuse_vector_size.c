// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: the 'vector_size' attribute is not supported
// A REFUSED row of docs/gnu-extensions.md. Sprint 36 shipped SSE2 and NEON
// vectorization but deliberately declined to invent a vector parameter
// contract for either psABI, so a vector_size type has no way to be passed
// or returned. The arm64 selector's matching refusal is the other end.
typedef int v4si __attribute__((vector_size(16)));
