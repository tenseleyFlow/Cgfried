// SKIP(*): staged for execution at Sprint 25 (no backend yet)
// 20 chained long double ops: x87 stack balance proven by running to
// completion (an unbalanced stack corrupts silently, functions later).
// EXIT_CODE: 42
int main(void) {
    volatile long double a = 1.0L;
    long double x = a;
    int i;
    for (i = 1; i <= 20; i++) {
        if (i % 3 == 0) x = x * (long double)i;
        else x = x + (long double)i;
    }
    /* 1 +1+2 *3 +4+5 *6 ... compute mod 256 down to a known exit */
    return (int)((unsigned long)x % 199) == 55 ? 42 : 1;
}
