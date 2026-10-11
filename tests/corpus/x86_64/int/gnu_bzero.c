// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 1 1
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}bzero
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}bzero
/* Darwin's FD_ZERO macro expands to __builtin_bzero. Keep the builtin
 * portable by lowering through memset, while evaluating both arguments once.
 */
int printf(const char *, ...);

static unsigned char bytes[] = {0x11, 0x22, 0x33, 0x44, 0x55};
static int destination_calls;
static int count_calls;

static void *next_destination(void)
{
    destination_calls++;
    return bytes + 1;
}

static int next_count(void)
{
    count_calls++;
    return 3;
}

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_bzero((void *)0, 0)), void),
               "bzero returns void");

int main(void)
{
    __builtin_bzero(next_destination(), next_count());
    int bytes_ok = bytes[0] == 0x11 && bytes[1] == 0 && bytes[2] == 0 &&
                   bytes[3] == 0 && bytes[4] == 0x55;

    printf("%d %d %d\n", bytes_ok, destination_calls, count_calls);
    return !bytes_ok || destination_calls != 1 || count_calls != 1;
}
