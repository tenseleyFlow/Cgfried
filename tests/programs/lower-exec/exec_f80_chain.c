// 20 chained long double ops: x87 stack balance proven by running to
// completion (an unbalanced stack corrupts silently, functions later).
// EXIT_CODE: 42
int main(void)
{
    volatile long double a = 1.0L;
    long double x = a;
    int i;
    for (i = 1; i <= 20; i++) {
        if (i % 3 == 0)
            x = x * (long double)i;
        else
            x = x + (long double)i;
    }
    /* the chain lands on 4187523 (gcc-verified); 4187523 % 199 = 165 */
    return (int)((unsigned long)x % 199) == 165 ? 42 : 1;
}
