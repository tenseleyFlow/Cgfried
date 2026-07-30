// FLAGS: -fsyntax-only
// ERROR_EXPECTED: did you mean 'uint32_t'?
typedef unsigned int uint32_t;
uint32_r x;
