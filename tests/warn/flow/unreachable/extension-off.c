// FLAGS: -fsyntax-only
// WARN_COUNT: 0
int flow_unreachable_extension_off(void)
{
    return 1;
    return 2;
}
