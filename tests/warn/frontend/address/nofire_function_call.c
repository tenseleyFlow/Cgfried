// FLAGS: -fsyntax-only -Waddress
// WARN_COUNT: 0
static int predicate(void) { return 1; }
int call_has_value(void)
{
    return predicate();
}
