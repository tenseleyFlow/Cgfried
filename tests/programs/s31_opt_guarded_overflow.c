// OPT_EQ: all
// EXIT_CODE: 0

int main(int argc, char **argv)
{
    volatile int min = (-2147483647 - 1);
    volatile int neg_one = -1;

    (void)argv;
    /* The forbidden pair is present in IR but unreachable in every normal
     * runner invocation. Folding must still never execute a host SIGFPE. */
    if (argc == 99)
        return min / neg_one;
    return 0;
}
