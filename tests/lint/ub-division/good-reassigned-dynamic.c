// OPT_EQ: all
// EXIT_CODE: 0
int main(int argc, char **argv)
{
    int divisor = 1 - 1;
    (void)argv;
    divisor = argc;
    return 12 / divisor == 12;
}
