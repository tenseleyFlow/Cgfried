// FLAGS: --target=arm64-linux -fsyntax-only -std=gnu17
// ERROR_EXPECTED: more than one register output on arm64 requires every extra
// output to use the general register class and have exactly one matching input
/* A read-write extra can span the asm through its matching input. This second
 * output is write-only, so it deliberately remains outside that narrow,
 * allocator-sound ARM64 contract. */
void f(void)
{
    int a;
    int b;

    __asm__("" : "=r"(a), "=r"(b));
}
