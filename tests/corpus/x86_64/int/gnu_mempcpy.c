// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 1 1 1 1
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}mempcpy
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}mempcpy
/* The builtin returns the end of the copied range without depending on a
 * host libc mempcpy symbol. All three operands are evaluated exactly once.
 */
int printf(const char *, ...);

static char destination[8];
static const char source[] = "abc";
static int destination_calls;
static int source_calls;
static int count_calls;

static void *next_destination(void)
{
    destination_calls++;
    return destination;
}

static const void *next_source(void)
{
    source_calls++;
    return source;
}

static int next_count(void)
{
    count_calls++;
    return 4;
}

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_mempcpy((void *)0, (const void *)0, 0)),
                   void *),
               "mempcpy returns void pointer");

int main(void)
{
    void *end =
        __builtin_mempcpy(next_destination(), next_source(), next_count());
    int end_ok = end == destination + 4;
    int bytes_ok = destination[0] == 'a' && destination[1] == 'b' &&
                   destination[2] == 'c' && destination[3] == 0;

    printf("%d %d %d %d %d\n", end_ok, bytes_ok, destination_calls,
           source_calls, count_calls);
    return !end_ok || !bytes_ok || destination_calls != 1 ||
           source_calls != 1 || count_calls != 1;
}
