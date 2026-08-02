// MS_CHECK: memsafe function=two_sites sites=2
// MS_CHECK: site=1 callee=malloc exit=0 state=allocated
// MS_CHECK: trace site=1 exit=0 event=alloc line=13 col=5 note=allocated here
// MS_CHECK: site=2 callee=calloc exit=0 state=freed
// MS_CHECK: trace site=2 exit=0 event=alloc line=14 col=5 note=allocated here
// MS_CHECK: trace site=2 exit=0 event=free line=16 col=5 note=freed here
void *malloc(unsigned long);
void *calloc(unsigned long, unsigned long);
void free(void *);

void two_sites(void)
{
    void *a = malloc(8);
    void *b = calloc(1, 8);
    (void)a;
    free(b);
}
