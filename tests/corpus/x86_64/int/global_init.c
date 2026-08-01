// OPT_EQ: all
// data/rodata emission + an addend reloc, read back at runtime.
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): .quad table+8
int table[4] = {10, 20, 30, 40};
int *mid = &table[2];
static const char msg[] = "ok";
int main(void)
{
    if (table[0] != 10 || table[3] != 40)
        return 1;
    if (*mid != 30)
        return 2;
    if (mid[-1] != 20)
        return 3;
    if (msg[0] != 'o' || msg[1] != 'k' || msg[2] != 0)
        return 4;
    return 0;
}
