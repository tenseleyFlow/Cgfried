// MS_CHECK: memsafe function=calloc_free sites=1
// MS_CHECK: site=1 callee=calloc exit=0 state=freed
// MS_CHECK: trace site=1 exit=0 event=alloc line=10 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=0 event=free line=11 col=5 note=freed here
void *calloc(unsigned long, unsigned long);
void free(void *);

void calloc_free(void)
{
    void *p = calloc(4, 8);
    free(p);
}
