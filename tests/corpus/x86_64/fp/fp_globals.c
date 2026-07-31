// FP globals: rodata constants and data images with double bit
// patterns, read back at runtime.
// EXIT_CODE: 0
double table[3] = {0.5, 1.5, 2.5};
static double acc = 0.25;
int main(void)
{
    if (table[0] + table[1] + table[2] != 4.5)
        return 1;
    acc = acc * 4.0;
    if (acc != 1.0)
        return 2;
    return 0;
}
