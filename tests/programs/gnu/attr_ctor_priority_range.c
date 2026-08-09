// FLAGS: -fsyntax-only
// ERROR_EXPECTED: constructor priorities must be integers from 0 to 65535
// The priority range, gcc's exactly: 0..65535 inclusive, and anything outside
// is an ERROR rather than a clamp. Silently clamping would reorder a program's
// initialization without saying so.
//
// The bound is also the DEFAULT: gcc emits the same plain `.init_array` for a
// bare `constructor` and for `constructor(65535)`, so the top of the range is
// a real priority rather than a sentinel meaning "none".
__attribute__((constructor(65536))) static void too_big(void)
{
}

__attribute__((destructor(-1))) static void negative(void)
{
}
