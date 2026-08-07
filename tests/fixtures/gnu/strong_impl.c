/* The strong definition that must win over attr_weak_overridden.c's weak
 * one. Kept in fixtures/ rather than programs/ so the runner compiles it as
 * an auxiliary TU instead of a test in its own right. */
int impl(void)
{
    return 7;
}
