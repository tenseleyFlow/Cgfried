// FLAGS: -fdump-sema -fno-common
// CHECK: var t1: int [external] [tentative] [zero-init]
// Under -fno-common the same tentative becomes a zero-initialized
// DEFINITION in this TU — gcc 10's default, implemented now because both
// symbol kinds must exist for Sprint 19 either way.
int t1;
