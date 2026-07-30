// FLAGS: -E
// ERROR_EXPECTED: virt.c:199
// ERROR_EXPECTED: in expansion of macro 'P'
// #line remaps the OUTERMOST use-site frame's displayed location.
#define P(a,b) a##b
#line 199 "virt.c"
int v = P(x,+);
