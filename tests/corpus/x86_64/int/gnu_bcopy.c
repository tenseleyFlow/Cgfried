// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 1 1 1
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}bcopy
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}bcopy
/* Source precedes destination; the overlapping copy is memmove-safe and
 * evaluates each argument exactly once without a host bcopy symbol.
 */
int printf(const char *, ...);

static char bytes[] = "abcdefg";
static int source_calls;
static int destination_calls;
static int count_calls;

static const void *next_source(void)
{
    source_calls++;
    return bytes;
}

static void *next_destination(void)
{
    destination_calls++;
    return bytes + 1;
}

static int next_count(void)
{
    count_calls++;
    return 4;
}

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_bcopy((const void *)0, (void *)0, 0)),
                   void),
               "bcopy returns void");

int main(void)
{
    __builtin_bcopy(next_source(), next_destination(), next_count());
    int bytes_ok = bytes[0] == 'a' && bytes[1] == 'a' && bytes[2] == 'b' &&
                   bytes[3] == 'c' && bytes[4] == 'd' && bytes[5] == 'f';

    printf("%d %d %d %d\n", bytes_ok, source_calls, destination_calls,
           count_calls);
    return !bytes_ok || source_calls != 1 || destination_calls != 1 ||
           count_calls != 1;
}
