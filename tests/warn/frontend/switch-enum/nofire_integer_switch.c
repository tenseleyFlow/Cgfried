// FLAGS: -fsyntax-only -Wswitch-enum
// WARN_COUNT: 0
int integer_enum_check(int value)
{
    switch (value) {
    case 0: return 1;
    default: return 0;
    }
}
