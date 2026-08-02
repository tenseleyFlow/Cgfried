// MS_CHECK: memsafe function=alloc_return sites=1
// MS_CHECK: site=1 callee=malloc exit=0 state=escaped
// MS_CHECK: trace site=1 exit=0 event=alloc line=9 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=0 event=return line=9 col=5 note=returned here
void *malloc(unsigned long);

void *alloc_return(void)
{
    return malloc(24);
}
