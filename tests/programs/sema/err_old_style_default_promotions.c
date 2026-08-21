// FLAGS: -fsyntax-only
// ERROR_EXPECTED: conflicting types
int takes_char();
int takes_char(char);

int takes_short(short);
int takes_short();

int takes_bool();
int takes_bool(_Bool);

int takes_float(float);
int takes_float();

int becomes_variadic();
int becomes_variadic(int, ...);

/* Definition-first must retain the resolved K&R signature for a later
 * prototype instead of collapsing it to an unspecified `f()` type. */
void kr_float_first(x) float x; { (void)x; }
void kr_float_first(float);

/* Unlike a declaration `f()`, an empty-list definition has exactly zero
 * parameters. Exercise both declaration orders. */
void empty_after_float(float);
void empty_after_float() {}

void empty_before_char() {}
void empty_before_char(char);
