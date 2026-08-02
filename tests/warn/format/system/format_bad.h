int printf(const char *, ...);
static void system_bad_format(void)
{
    printf("%s", 1);
}
