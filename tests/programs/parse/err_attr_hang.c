// FLAGS: -fsyntax-only
// WARNING_EXPECTED: 'x' attribute directive ignored
// REGRESSION (Sprint 11): this pair used to HANG. The K&R declaration
// loop terminated on parse_declaration returning NULL, and recovery
// changed that function to return a poisoned error node instead — so the
// loop spun forever on a declaration that consumed nothing. Every loop
// over a recovering production needs its own cursor-progress guard; a
// callee's return value is not a termination condition.
// Found by the c-testsuite differential (00210.c), not by the fuzzer,
// whose corpus did not contain this shape.
//
// It asserted an ERROR until Sprint 55, only because every attribute was
// one. `x` is an UNKNOWN attribute, which gcc accepts with a warning and so
// do we — a compiler that rejects a name it has never heard of cannot read
// next year's headers. The hang is what this fixture is for, and a run that
// terminates at all still proves it; the warning just pins that the
// declaration was really parsed rather than skipped.
void foo(void) __attribute__((x));
void __attribute__((x)) foo(void) {}
