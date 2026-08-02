// MS_CHECK: memsafe function=read_use sites=1
// MS_CHECK: site=1 callee=malloc exit=0 state=freed
// MS_CHECK: trace site=1 exit=0 event=alloc line=11 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=0 event=use line=12 col=17 note=read through pointer here
// MS_CHECK: trace site=1 exit=0 event=free line=13 col=5 note=freed here
void *malloc(unsigned long);
void free(void *);

int read_use(void)
{
    int *p = malloc(4);
    int value = *p;
    free(p);
    return value;
}
