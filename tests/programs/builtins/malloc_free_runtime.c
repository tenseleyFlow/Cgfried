// __builtin_malloc and __builtin_free use the hosted libc signatures and
// preserve their operand evaluations exactly once.
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}malloc
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}free
static int size_calls;
static int pointer_calls;
static char *allocation;

static unsigned char allocation_size(void)
{
    size_calls++;
    return 16;
}

static char *released_pointer(void)
{
    pointer_calls++;
    return allocation;
}

int main(void)
{
    allocation = __builtin_malloc(allocation_size());
    if (!allocation)
        return 1;
    if (size_calls != 1)
        return 2;
    allocation[0] = 17;
    allocation[15] = 29;
    if (allocation[0] != 17 || allocation[15] != 29)
        return 3;
    __builtin_free(released_pointer());
    if (pointer_calls != 1)
        return 4;
    __builtin_free((void *)0);
    return 0;
}
