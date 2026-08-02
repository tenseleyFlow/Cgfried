// FLAGS: -fsyntax-only -Wswitch-default
// WARN_COUNT: 1
int needs_default(int value)
{
    // WARN_CHECK: switch-default switch missing default case
    switch (value) {
    case 0: return 1;
    }
    return 0;
}
