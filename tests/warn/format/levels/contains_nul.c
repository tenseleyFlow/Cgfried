// FLAGS: -fsyntax-only -Wformat -Wno-format-extra-args
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format-contains-nul embedded '\0' in format
    printf("prefix\0%d", 1);
}
