// FLAGS: -fsyntax-only
// ERROR_EXPECTED: overflow in constant expression
static int x;
static int *p = &x + ((-9223372036854775807LL - 1) / -1);
