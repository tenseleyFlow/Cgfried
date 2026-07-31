// main is callable in C (unlike C++): recursion through main links.
// EXIT_CODE: 10
int main(int argc, char **argv)
{
    (void)argv;
    if (argc >= 10)
        return argc;
    return main(argc + 1, argv);
}
