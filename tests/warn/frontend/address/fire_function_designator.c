// FLAGS: -fsyntax-only -Waddress
// WARN_COUNT: 1
static void callback(void) {}
int function_is_nonnull(void)
{
    // WARN_CHECK: address the address of 'callback' will never be NULL
    if (callback)
        return 1;
    return 0;
}
