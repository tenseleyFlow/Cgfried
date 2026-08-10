// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 15 17
/* glibc's x86 <sys/io.h> has exactly this bounded multi-output shape:
 * fixed =D and =c outputs, each kept live at the asm by a matching input.
 * The second output is captured after the template and stored back to C.
 * Living in the corpus makes the same code execute under CGF_SPILL_ALL=1;
 * the arm64 lanes take the plain-C branch because general multi-output asm
 * remains an explicit backend boundary there. */
extern int printf(const char *, ...);

static void fixed_pair(int a, int b, int *left, int *right)
{
#if defined(__x86_64__)
    int x;
    int y;

    __asm__("addl $5, %0\n\tsubl $3, %1" : "=D"(x), "=c"(y) : "0"(a), "1"(b));
    *left = x;
    *right = y;
#elif defined(__aarch64__)
    *left = a + 5;
    *right = b - 3;
#else
#error "no test implementation for this target"
#endif
}

int main(void)
{
    int left;
    int right;

    fixed_pair(10, 20, &left, &right);
    printf("%d %d\n", left, right);
    return left == 15 && right == 17 ? 0 : 1;
}
