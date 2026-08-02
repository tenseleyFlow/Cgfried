// MS_CHECK: memsafe function=alloc_live sites=1
// MS_CHECK: site=1 callee=malloc exit=0 state=allocated
// MS_CHECK: trace site=1 exit=0 event=alloc line=8 col=5 note=allocated here
void *malloc(unsigned long);

void alloc_live(void)
{
    void *p = malloc(32);
    (void)p;
}
