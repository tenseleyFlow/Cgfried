// FLAGS: -fsyntax-only -Wswitch-enum
// WARN_COUNT: 1

enum switch_enum_color {
    SWITCH_ENUM_RED,
    SWITCH_ENUM_BLUE
};

int switch_enum_out_of_range(enum switch_enum_color color)
{
    switch (color) {
    case SWITCH_ENUM_RED:
        return 1;
    case SWITCH_ENUM_BLUE:
        return 2;
    // WARN_CHECK: switch-enum case value '7' not in enumerated type
    case 7:
        return 3;
    default:
        return 0;
    }
}
