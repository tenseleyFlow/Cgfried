// FLAGS: -emit-ir
// ERROR_EXPECTED: a variable-length array declaration is not lowered yet: lands in Sprint 20
int f(int n) { int a[n]; return 0; }
