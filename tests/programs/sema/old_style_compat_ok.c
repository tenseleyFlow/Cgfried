// FLAGS: -fsyntax-only
// An empty identifier-list declaration remains compatible when every
// prototype parameter already has its default-promoted type. Both declaration
// orders and a compatible K&R definition are controls for SEMA-H-03.
int takes_int();
int takes_int(int);

int takes_unsigned(unsigned int);
int takes_unsigned();

int takes_double();
int takes_double(double);

int takes_long_double(long double);
int takes_long_double();

int takes_pointer();
int takes_pointer(const char *);

int takes_qualified();
int takes_qualified(const int);

enum small_value { SMALL_ZERO, SMALL_ONE };
int takes_enum();
int takes_enum(enum small_value);

int kr_definition(int, double);
int kr_definition(i, d) int i; double d; { return i + (int)d; }

int kr_definition_first(i, d) int i; float d; { return i + (int)d; }
int kr_definition_first(int, double);

int empty_definition(void);
int empty_definition() { return 0; }

/* K&R declaration-list types are resolved in function scope and source
 * order, so a later VLA parameter can see an earlier parameter. */
int kr_dependent_vla(n, a) int n; int a[n]; { return a[0]; }
int kr_source_order(a, n) int n; int a[n]; { return a[0]; }
