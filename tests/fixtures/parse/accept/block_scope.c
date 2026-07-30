typedef int T;
void f(void) {
    { T T; T = 1; (void)T; }
    T y = 0;
    (void)y;
    for (int T = 0; T < 1; T++)
        ;
    { T z = 0; (void)z; }
}
