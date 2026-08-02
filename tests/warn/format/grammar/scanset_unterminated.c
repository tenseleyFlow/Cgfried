// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int scanf(const char *, ...);
void test(void)
{
    char out[8];
    // WARN_CHECK: format no closing ']' bracket in scanf format
    scanf("%[abc", out);
}
