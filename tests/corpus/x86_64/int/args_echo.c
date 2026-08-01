// OPT_EQ: all
// argc/argv arrive through crt1's protocol — main(int, char**).
// EXIT_CODE: 1
int main(int argc, char **argv)
{
    if (!argv[0][0])
        return 100; /* argv[0] is a nonempty path */
    if (argv[argc] != 0)
        return 101; /* argv is null-terminated */
    return argc;    /* the runner passes no args */
}
