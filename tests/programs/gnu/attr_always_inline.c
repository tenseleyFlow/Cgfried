// FLAGS: -S -std=gnu17 -O0
// ASM_CHECK(x86_64-linux-gnu): {{^retained:}}
// ASM_CHECK-NOT(x86_64-linux-gnu): {{^inline_only:}}
// ASM_CHECK(x86_64-linux-gnu): .quad{{[ \t]+}}inline_only
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}inline_only
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}retained
/* always_inline is mandatory at -O0.  The ordinary C inline matrix remains
 * independent: `inline_only` supplies no external definition and therefore
 * disappears after its calls are spliced, while `retained` is an extern
 * inline definition and keeps its legal out-of-line body. */
inline __attribute__((always_inline)) int inline_only(int x)
{
    return x + 1;
}

/* Taking the address still denotes the external fallback promised by C's
 * inline rules; it must not make this TU emit the inline-only body. */
int (*fallback)(int) = inline_only;

extern inline __attribute__((__always_inline__)) int retained(int x)
{
    return x * 2;
}

int main(void)
{
    return inline_only(20) + retained(10) == 41 ? 0 : 1;
}
