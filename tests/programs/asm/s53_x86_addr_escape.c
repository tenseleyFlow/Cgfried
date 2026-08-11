// FLAGS: -O2 -S
// The computed pointer is returned on one path and participates in arithmetic
// on the other.  That non-address use must keep the ptradd (and its scaled
// producer) materialized; folding the load is allowed but suppressing the SSA
// definitions would leave the escaping value undefined.
// ASM_CHECK(x86_64-linux-gnu): shlq{{[ \t]+}}$2,
// ASM_CHECK(x86_64-linux-gnu): leaq{{[ \t]+}}({{%r[a-z0-9]+}},{{%r[a-z0-9]+}},4),
long s53_addr_escape(const int *p, unsigned long i, int choose)
{
    const int *q = p + i;

    if (choose)
        return *q + (long)q;
    return (long)q;
}
