// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int parentheses_grouped(int a, int b, int c) { return (a && b) || c; }
