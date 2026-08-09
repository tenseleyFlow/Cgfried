// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: more than one register output is not supported yet
/* The remaining boundary of extended asm, and it is drawn where the data
 * said to draw it rather than where it was convenient.
 *
 * An IR instruction defines at most one value and the shared MIR view
 * reports one def per instruction, so a second REGISTER output would mean
 * widening that interface across both backends. Counting musl's asm sites
 * for the two targets we have says that is rarely what a second output is:
 * x86_64 has 181 one-output sites against 27 two-output, aarch64 172
 * against 19, and the two-output cases are dominated by a MEMORY second
 * output (`"=m"`, `"=Q"` in the atomics) which consumes no register at all.
 *
 * So one register output plus any number of memory outputs covers the
 * campaign, and this is the case that is left. A memory output is
 * unaffected -- see asm_mem_output.c. */
void f(int *p, int *q)
{
    int a, b;

    __asm__("" : "=r"(a), "=r"(b));
    *p = a;
    *q = b;
}
