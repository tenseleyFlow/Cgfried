// ERROR_EXPECTED: invalid storage class in function parameter
// A malformed parameter must stop in the front end, never reach lowering.
int f(static void) { return 0; }
