// FLAGS: -fsyntax-only -Wunreachable-code
// DIVERGES(gcc-8): GCC 8 accepts -Wunreachable-code as a no-op.
// WARN_COUNT: 2
int flow_unreachable_region_one(void)
{
    return 1;
    // WARN_CHECK: unreachable-code code will never be executed
    return 2;
    return 3;
}
int flow_unreachable_region_two(void)
{
    goto done;
    return 4;
    return 5;
done:
    return 6;
}
