// MS_CHECK: memsafe function=null_branch sites=1
// MS_CHECK: block=2 name=if.join2 states=2
// MS_CHECK: site=1 callee=malloc exit=0 state=unallocated
// MS_CHECK: trace site=1 exit=0 event=alloc line=15 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=0 event=branch line=16 col=5 note=pointer is null on this branch
// MS_CHECK: site=1 callee=malloc exit=1 state=freed
// MS_CHECK: trace site=1 exit=1 event=alloc line=15 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=1 event=branch line=16 col=5 note=pointer is non-null on this branch
// MS_CHECK: trace site=1 exit=1 event=free line=17 col=9 note=freed here
void *malloc(unsigned long);
void free(void *);

void null_branch(void)
{
    void *p = malloc(48);
    if (p != (void *)0)
        free(p);
}
