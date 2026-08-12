// FLAGS: -O2
// Optimized x86 lowering must keep x87 long-double loop state in memory.
// EXIT_CODE: 42
int main(void)
{
    long double value = 1.0L;
    int i;

    for (i = 0; i < 8; i++)
        value = value * 2.0L + 1.0L;
    return value == 511.0L ? 42 : 1;
}
