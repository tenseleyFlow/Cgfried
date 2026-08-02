// MS_CHECK: memsafe function=global_escape sites=1
// MS_CHECK: site=1 callee=malloc exit=0 state=escaped
// MS_CHECK: trace site=1 exit=0 event=alloc line=10 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=0 event=escape line=10 col=5 note=stored into escaping memory here
void *malloc(unsigned long);
void *global_pointer;

void global_escape(void)
{
    global_pointer = malloc(40);
}
