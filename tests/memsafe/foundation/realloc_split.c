// MS_CHECK: memsafe function=realloc_split sites=2
// MS_CHECK: site=1 callee=malloc exit=0 state=freed
// MS_CHECK: trace site=1 exit=0 event=alloc line=21 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=0 event=realloc line=23 col=5 note=old pointer freed when realloc succeeded
// MS_CHECK: site=1 callee=malloc exit=1 state=freed
// MS_CHECK: trace site=1 exit=1 event=alloc line=21 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=1 event=free line=26 col=9 note=freed here
// MS_CHECK: site=2 callee=realloc exit=0 state=freed
// MS_CHECK: trace site=2 exit=0 event=realloc line=22 col=5 note=reallocated here
// MS_CHECK: trace site=2 exit=0 event=branch line=23 col=5 note=pointer is non-null on this branch
// MS_CHECK: trace site=2 exit=0 event=free line=24 col=9 note=freed here
// MS_CHECK: site=2 callee=realloc exit=1 state=unallocated
// MS_CHECK: trace site=2 exit=1 event=realloc line=22 col=5 note=reallocated here
// MS_CHECK: trace site=2 exit=1 event=branch line=23 col=5 note=pointer is null on this branch
void *malloc(unsigned long);
void *realloc(void *, unsigned long);
void free(void *);

void realloc_split(void)
{
    void *p = malloc(12);
    void *q = realloc(p, 24);
    if (q != (void *)0)
        free(q);
    else
        free(p);
}
