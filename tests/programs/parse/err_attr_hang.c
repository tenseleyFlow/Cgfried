// FLAGS: -fsyntax-only
// ERROR_EXPECTED: Sprint 55
// REGRESSION (Sprint 11): this pair used to HANG. The K&R declaration
// loop terminated on parse_declaration returning NULL, and recovery
// changed that function to return a poisoned error node instead — so the
// loop spun forever on a declaration that consumed nothing. Every loop
// over a recovering production needs its own cursor-progress guard; a
// callee's return value is not a termination condition.
// Found by the c-testsuite differential (00210.c), not by the fuzzer,
// whose corpus did not contain this shape.
void foo(void) __attribute__((x));
void __attribute__((x)) foo(void) { }
