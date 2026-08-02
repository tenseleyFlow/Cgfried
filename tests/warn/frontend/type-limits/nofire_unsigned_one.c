// FLAGS: -fsyntax-only -Wtype-limits
// WARN_COUNT: 0
int unsigned_at_least_one(unsigned value)
{
    return value >= 1;
}
