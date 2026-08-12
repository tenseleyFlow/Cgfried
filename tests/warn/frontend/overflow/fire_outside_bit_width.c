// FLAGS: -fsyntax-only -Woverflow
// WARN_COUNT: 2

// WARN_CHECK: overflow changes value from '4294967296'
int overflow_outside_int_width = 4294967296UL;
// WARN_CHECK: overflow changes value from '-300'
unsigned char overflow_below_unsigned_width = -300;
