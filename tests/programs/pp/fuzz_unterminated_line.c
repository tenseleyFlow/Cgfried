// FLAGS: -E
// ERROR_EXPECTED: missing terminating
// ppfuzz seed 957: an unterminated string in a directive yields a 1-char
// token; len-2 underflowed to ~4G and hung the compiler.
#line 99 "