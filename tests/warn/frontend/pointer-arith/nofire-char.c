// FLAGS: -fsyntax-only -Wpointer-arith
// WARN_COUNT: 0
char *pointer_arith_char(char *p) { return p + 1; }
