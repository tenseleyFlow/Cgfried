// FLAGS: -emit-ir
// ERROR_EXPECTED: an _Atomic object access is not lowered yet: lands in Sprint 20
_Atomic int a;
int f(void) { return a; }
