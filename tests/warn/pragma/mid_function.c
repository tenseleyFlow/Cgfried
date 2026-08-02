// FLAGS: -fsyntax-only
// WARN_COUNT: 1
int mid_function(void)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic bogus
#pragma GCC diagnostic pop
#pragma GCC diagnostic bogus
    return 0;
}
