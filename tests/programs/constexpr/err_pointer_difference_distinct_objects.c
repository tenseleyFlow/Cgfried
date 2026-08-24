// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: an address is not an integer constant expression

static int left;
static int right;
static long delta = &left - &right;
