// FLAGS: -fsyntax-only -Wunreachable-code
// WARN_COUNT: 0
int flow_unreachable_sizeof_config(void)
{
    if (sizeof(long) == 4)
        return 1;
    return 2;
}
