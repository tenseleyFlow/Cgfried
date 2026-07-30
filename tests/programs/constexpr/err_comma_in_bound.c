// FLAGS: -fsyntax-only
// ERROR_EXPECTED: variably modified type at file scope
// A comma-expression bound is not an ICE, so the array is a VLA — and a
// VLA at file scope is the error, exactly as gcc diagnoses it. (The
// comma is SEPARATELY illegal in contexts that require an ICE: see the
// enum fixtures.)
int a[(1,2)];
