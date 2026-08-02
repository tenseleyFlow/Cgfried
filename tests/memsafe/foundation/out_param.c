// MS_CHECK: memsafe function=out_param sites=1
// MS_CHECK: site=1 callee=posix_memalign exit=0 state=allocated
// MS_CHECK: trace site=1 exit=0 event=alloc line=9 col=5 note=allocated here
int posix_memalign(void **, unsigned long, unsigned long);

void out_param(void)
{
    void *p;
    (void)posix_memalign(&p, 16, 64);
    (void)p;
}
