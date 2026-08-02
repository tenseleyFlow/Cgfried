// FLAGS: -fsyntax-only -Wswitch-enum
// WARN_COUNT: 1
enum state { IDLE, RUNNING };
int enum_default(enum state state)
{
    // WARN_CHECK: switch-enum enumeration value 'RUNNING' not handled in switch
    switch (state) {
    case IDLE: return 0;
    default: return 1;
    }
}
