// OPT_EQ: all
// 6 dense cases: the jump table, executed.
// EXIT_CODE: 21
// ASM_CHECK(x86_64-linux-gnu): jmp *(
static int pick(int n)
{
    switch (n) {
    case 0:
        return 1;
    case 1:
        return 2;
    case 2:
        return 4;
    case 3:
        return 8;
    case 4:
        return 16;
    case 5:
        return 32;
    default:
        return 0;
    }
}
int main(void)
{
    return pick(0) + pick(2) + pick(4) + pick(9);
}
