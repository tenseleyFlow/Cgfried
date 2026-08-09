// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: a braced-group within an expression is allowed only inside
// a function
/* gcc: "braced-group within expression allowed only inside a function".
 * There is no block to run at file scope, and the initializer it would feed
 * has to be a constant anyway. Refused in the PARSER, where the construct is
 * recognized, rather than left to produce a cascade downstream. */
int g = ({ 1; });
