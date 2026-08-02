// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
// DIVERGES(gcc-8): -Wbuiltin-declaration-mismatch is outside Sprint 39.
int vprintf(const char *);
void test(void)
{
    vprintf("%r");
}
