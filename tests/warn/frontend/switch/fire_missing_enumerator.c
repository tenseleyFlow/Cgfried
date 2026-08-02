// FLAGS: -fsyntax-only -Wswitch
// WARN_COUNT: 1
enum color { RED, GREEN, BLUE };
int missing_blue(enum color color)
{
    // WARN_CHECK: switch enumeration value 'BLUE' not handled in switch
    switch (color) {
    case RED: return 1;
    case GREEN: return 2;
    }
    return 0;
}
