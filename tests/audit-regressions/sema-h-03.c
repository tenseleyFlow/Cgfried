// RESOLVED(audit): SEMA-H-03 old-style function compatibility ignores default promotions
int takes_char();
int takes_char(char);
int takes_float();
int takes_float(float);
int becomes_variadic();
int becomes_variadic(int, ...);
