// FLAGS: -O2 -S
// A single scaled pointer dominates both successor blocks.  Selection must
// carry its IR address shape across the block boundary instead of emitting a
// shl+lea in entry and using the temporary in each load.
// ASM_CHECK(x86_64-linux-gnu): movl{{[ \t]+}}({{%r[a-z0-9]+}},{{%r[a-z0-9]+}},4)
// ASM_CHECK(x86_64-linux-gnu): movl{{[ \t]+}}4({{%r[a-z0-9]+}},{{%r[a-z0-9]+}},4)
// ASM_CHECK-NOT(x86_64-linux-gnu): shlq
// ASM_CHECK-NOT(x86_64-linux-gnu): leaq{{[ \t]+}}({{%r[a-z0-9]+}},{{%r[a-z0-9]+}},4)
int s53_addr_cross_block(const int *p, unsigned long i, int choose)
{
    const int *q = p + i;

    if (choose)
        return q[0];
    return q[1];
}
