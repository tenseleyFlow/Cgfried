// FLAGS: -fsyntax-only -Wswitch
// WARN_COUNT: 1

enum switch_color {
    SWITCH_RED,
    SWITCH_BLUE
};

int switch_out_of_range(enum switch_color color)
{
    switch (color) {
    case SWITCH_RED:
        return 1;
    case SWITCH_BLUE:
        return 2;
    // WARN_CHECK: switch case value '7' not in enumerated type
    case 7:
        return 3;
    }
    return 0;
}
