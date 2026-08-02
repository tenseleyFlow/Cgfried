// FLAGS: -fsyntax-only -std=c99
// WARN_COUNT: 0
int declared_function(void);
int implicit_function_declared(void) { return declared_function(); }
