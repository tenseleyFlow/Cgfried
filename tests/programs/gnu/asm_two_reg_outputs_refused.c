// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: more than one register output is not supported yet
/* The remaining boundary of extended asm is allocator-chosen extra outputs.
 *
 * An IR instruction defines at most one value and the shared MIR view
 * reports one def per instruction, so a second REGISTER output would mean
 * widening that interface across both backends. Counting musl's asm sites
 * for the two targets we have says that is rarely what a second output is:
 * x86_64 has 181 one-output sites against 27 two-output, aarch64 172
 * against 19, and the two-output cases are dominated by a MEMORY second
 * output (`"=m"`, `"=Q"` in the atomics) which consumes no register at all.
 *
 * Memory outputs are unaffected, and x86 fixed-register extras with exactly
 * one matching input are supported because their location is known without a
 * second MIR def -- see asm_mem_output.c and asm_multi_fixed.c. The two `=r`
 * outputs below still require two simultaneous allocator choices and remain
 * a hard boundary. */
void f(int *p, int *q)
{
    int a, b;

    __asm__("" : "=r"(a), "=r"(b));
    *p = a;
    *q = b;
}
