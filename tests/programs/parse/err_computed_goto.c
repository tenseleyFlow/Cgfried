// FLAGS: --dump-ast
// ERROR_EXPECTED: computed goto is not supported
// The message no longer names a sprint. Sprint 55 decided computed goto is
// REFUSED rather than deferred -- see docs/gnu-extensions.md -- and a
// deferral that will never be met is exactly the lie the deferral reckoning
// removed. tests/programs/gnu/refuse_computed_goto.c is the tier fixture;
// this one stays because it also pins that the error survives --dump-ast.
void f(void *p) { goto *p; }
