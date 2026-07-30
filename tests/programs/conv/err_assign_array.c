// FLAGS: -fsyntax-only
// ERROR_EXPECTED: assignment to an array type
int gi; long gl; int *gip; long *glp; char *gp; const char *gcp;
void *gvp; char gc; _Bool gb; int garr[4]; const int gci = 0;
void (*gfp)(void);
struct A { int x; }; struct B { int x; };
struct A ga; struct B gbv;
void f(void) { garr = gip; }
