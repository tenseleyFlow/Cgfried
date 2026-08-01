// OPT_EQ: all
// Byte loop over a literal.
// EXIT_CODE: 12
static int len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}
int main(void)
{
    return len("hello, world");
}
