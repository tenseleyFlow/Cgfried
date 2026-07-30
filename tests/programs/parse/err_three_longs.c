// FLAGS: --dump-ast
// ERROR_EXPECTED: 'long long long' is too long
// Two is the limit (C11 6.7.2p2 lists `long long int` and no more). The
// message must name the offending specifier, not just say "syntax error".
long long long x;
