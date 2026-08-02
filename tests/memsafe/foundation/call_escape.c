// MS_CHECK: memsafe function=call_escape sites=1
// MS_CHECK: site=1 callee=strdup exit=0 state=escaped
// MS_CHECK: trace site=1 exit=0 event=alloc line=10 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=0 event=call line=11 col=5 note=passed to an unknown call here
char *strdup(const char *);
void consume(char *);

void call_escape(void)
{
    char *p = strdup("cgf");
    consume(p);
}
