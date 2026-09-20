// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 1 1
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}__builtin_extract_return_addr
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}__builtin_extract_return_addr
/* GCC's __builtin_extract_return_addr converts through void *, evaluates its
 * operand exactly once, and is a pointer-bit identity on both supported ABIs.
 * Keeping this executable fixture in the corpus exercises x86-64, spill-all,
 * native ARM64, and emulated ARM64 without changing the frontend fuzz corpus.
 */
int printf(const char *, ...);

static int object;
static int calls;

static void *next_address(void)
{
    calls++;
    return &object;
}

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_extract_return_addr((void *)0)),
                   void *),
               "extract_return_addr yields void pointer");

int main(void)
{
    void *decoded = __builtin_extract_return_addr(next_address());
    void *null_address = __builtin_extract_return_addr(0);

    printf("%d %d %d\n", decoded == &object, calls, null_address == 0);
    return decoded != &object || calls != 1 || null_address != 0;
}
