// FLAGS: -fsyntax-only -Wtype-limits
// WARN_COUNT: 0

enum type_limits_state {
    TYPE_LIMITS_OFF,
    TYPE_LIMITS_ON
};

int type_limits_enum(enum type_limits_state state)
{
    return state <= TYPE_LIMITS_ON;
}
