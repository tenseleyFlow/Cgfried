// FLAGS: -fsyntax-only -Wunreachable-code
// WARN_COUNT: 0
#define FLOW_PLATFORM_HAS_FEATURE 0
int flow_unreachable_macro_config(void)
{
    if (FLOW_PLATFORM_HAS_FEATURE)
        return 1;
    return 2;
}
