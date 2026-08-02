// MS_CHECK: memsafe function=inline_alloc_free sites=1
// MS_CHECK: site=1 callee=malloc exit=0 state=freed
// MS_CHECK: trace site=1 exit=0 event=alloc line=10 col=5 note=allocated here
// MS_CHECK: trace site=1 exit=0 event=free line=11 col=5 note=freed here
void *malloc(unsigned long);
void free(void *);

inline void inline_alloc_free(void)
{
    void *p = malloc(32);
    free(p);
}

void inline_user(void)
{
    inline_alloc_free();
}
