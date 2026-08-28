// FLAGS: -std=gnu89
// EXIT_CODE: 0
// ASM_CHECK-NOT: call{{[ \t]+}}alloca

int main(void)
{
    int *slot = (int *)alloca(sizeof(*slot));

    *slot = 42;
    return *slot != 42;
}
