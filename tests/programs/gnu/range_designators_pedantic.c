// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: ISO C forbids specifying range of elements to initialize
// WARN_COUNT: 1
// WARN_CHECK: pedantic ISO C forbids specifying range of elements to initialize
int warned[3] = {[0 ... 2] = 1};

__extension__ int quiet[3] = {[0 ... 2] = 1};
