// FLAGS: -fsyntax-only -Wswitch
// WARN_COUNT: 0
enum direction { NORTH, SOUTH };
int default_covers(enum direction direction)
{
    switch (direction) {
    case NORTH: return 1;
    default: return 0;
    }
}
