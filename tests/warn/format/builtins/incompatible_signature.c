// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
// DIVERGES(gcc-8): -Wbuiltin-declaration-mismatch is outside Sprint 39.
int fprintf(int, const char *, ...);
void test(void)
{
    fprintf(1, "%d", "wrong");
}
