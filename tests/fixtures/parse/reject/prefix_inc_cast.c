/* Prefix ++ takes a unary-expression; a cast is not one. */
typedef int T;
int x;
int f(void) { return ++(T)x; }
