// FLAGS: -fdump-sema
// CHECK: TAG Color: enum Color [underlying int]
// CHECK: TAG Big: enum Big [underlying unsigned int]
// CHECK: typedef size_t: unsigned long [none]
// CHECK: ENUMCONST RED = 0: int
// CHECK: ENUMCONST GREEN = 5: int
// CHECK: ENUMCONST BLUE = 6: int
// CHECK: var internal_var: int [internal] [tentative]
// CHECK: var extern_var: int [external]
// CHECK: var arr: int [10] [external] [tentative]
// CHECK: func f: int (int, char *) [external]
// CHECK: var cs: const char * const [external] [tentative]
// CHECK: var md: int [2] [3] [external] [tentative]
// The composite type is the observable: `int arr[]` then `int arr[10]`
// resolves to int[10], and an unprototyped `int f()` followed by a
// prototype keeps the prototype (6.2.7p3).
typedef unsigned long size_t;
enum Color { RED, GREEN = 5, BLUE };
enum Big { B1 = 3000000000 };
static int internal_var;
extern int extern_var;
int arr[];
int arr[10];
int f();
int f(int, char *);
const char *const cs;
int md[2][3];
