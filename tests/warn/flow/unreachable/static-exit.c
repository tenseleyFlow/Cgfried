// FLAGS: -fsyntax-only -Wunreachable-code
// DIVERGES(gcc-8): GCC 8 treats an internal exit spelling as a builtin declaration mismatch.
// WARN_COUNT: 0
static void exit(void)
{
}
int flow_static_exit_reachable(void)
{
    exit();
    return 1;
}
