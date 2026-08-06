/* Cgfried compiles this CALLER. clang compiles the callee. Apple's ABI puts
 * every anonymous argument on the stack; AAPCS64 fills x0-x7 first. Get the
 * boundary wrong and the callee reads the wrong slots. */
int sum9(int n, ...);
double avg3(int n, ...);
long mix(int a, int b, ...);
int printf(const char *, ...);

int main(void)
{
    printf("sum9=%d\n", sum9(9, 1, 2, 3, 4, 5, 6, 7, 8, 9));
    printf("avg3=%.4f\n", avg3(3, 1.5, 2.5, 6.5));
    printf("mix=%ld\n", mix(100, 200, 1L, 2L, 3L));
    printf("many=%d %d %d %d %d %d %d %d %d %d\n", 1, 2, 3, 4, 5, 6, 7, 8, 9,
           10);
    printf("mixed=%d %.1f %d %.1f %s\n", 1, 2.5, 3, 4.5, "tail");
    return 0;
}
