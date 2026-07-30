// FLAGS: --dump-tokens
// ERROR_EXPECTED: invalid digit '9' in octal constant
// ERROR_EXPECTED: in expansion of macro 'BAD'
// The Sprint-7 payoff: a LEXER diagnostic inside a macro expansion gets
// the expansion backtrace for free, because spans and diagnostics both
// route through the preprocessor's location table.
#define BAD 09
int x = BAD;
