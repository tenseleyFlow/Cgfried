/* A cast-expression is not a unary-expression, so sizeof stops after the
 * parenthesized type name and `(x)` is stray. */
typedef int T;
int x;
int f(void) { return sizeof (T)(x); }
