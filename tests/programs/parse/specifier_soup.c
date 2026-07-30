// FLAGS: --dump-ast
// CHECK: DECL a: unsigned long long
// CHECK: DECL b: unsigned long long
// CHECK: DECL c: unsigned long long
// CHECK: DECL d: long double
// CHECK: DECL e: signed char
// CHECK: DECL f: unsigned short
// CHECK: DECL g: const int [static]
// C11 6.7.2p2 defines the valid type specifiers as a MULTISET, so a, b and
// c below are the same type spelled three ways. Reducing by sorted multiset
// rather than by a state machine is what makes order-independence free —
// including the storage class and qualifiers mixed in among them.
unsigned long long a;
long long unsigned int b;
long unsigned long c;
double long d;
char signed e;
short unsigned f;
static const int g;
