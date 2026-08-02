// MS_CHECK: memsafe function=captured_call_escape sites=1
// MS_CHECK: site=1 callee=malloc exit=0 state=escaped
// MS_CHECK: trace site=1 exit=0 event=alloc line=10 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=0 event=call line=14 col=5 note=passed to an unknown call here
void *malloc(unsigned long);
void consume(void *);

void captured_call_escape(void)
{
    void *p = malloc(24);
    void *box[1];

    box[0] = p;
    consume(box);
}
