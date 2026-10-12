// FLAGS: --target=x86_64-linux-gnu -fsyntax-only -std=gnu17
// ERROR_EXPECTED: more than one register output is not supported yet
/* The remaining x86 boundary of extended asm is allocator-chosen write-only
 * extra outputs. ARM64's identical write-only boundary has its own
 * target-explicit fixture in asm_multi_output_arm64_refused.c.
 *
 * An IR instruction defines at most one value and the shared MIR view
 * reports one def per instruction, so a second REGISTER output would mean
 * widening that interface across both backends. Counting musl's asm sites
 * for the two targets we have says that is rarely what a second output is:
 * x86_64 has 181 one-output sites against 27 two-output, aarch64 172
 * against 19, and the two-output cases are dominated by a MEMORY second
 * output (`"=m"`, `"=Q"` in the atomics) which consumes no register at all.
 *
 * Memory outputs are unaffected. Fixed-register x86 extras are supported
 * because their locations are already known, and general read-write extras
 * on both backends share a local vreg with their one matching input. The two
 * `=r` outputs below have no spanning values, still require two simultaneous
 * allocator choices, and remain a hard boundary. */
void f(int *p, int *q)
{
    int a, b;

    __asm__("" : "=r"(a), "=r"(b));
    *p = a;
    *q = b;
}
