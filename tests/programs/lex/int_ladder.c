// FLAGS: --dump-tokens
// CHECK: INT_CONST 42 int
// CHECK: INT_CONST 2147483648 long
// CHECK: INT_CONST 2147483648 unsigned int
// CHECK: INT_CONST 4294967295 unsigned int
// CHECK: INT_CONST 1 unsigned long long
// THE asymmetry: an unsuffixed DECIMAL climbs signed rungs only, so
// 2147483648 is long; the same value in HEX may land on unsigned int.
42 2147483648 0x80000000 0xffffffff 1ull
