// FLAGS: --dump-tokens
// ERROR_EXPECTED: is not a valid universal character
// A UCN may not name a character below U+00A0 (6.4.3p2) - \u0041 is A.
"\u0041"
