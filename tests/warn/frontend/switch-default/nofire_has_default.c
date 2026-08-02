// FLAGS: -fsyntax-only -Wswitch-default
// WARN_COUNT: 0
int has_default(int value)
{
    switch (value) {
    case 0: return 1;
    default: return 0;
    }
}
