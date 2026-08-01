// OPT_EQ: all
// Byte store loop + compare loop.
// EXIT_CODE: 0
static void cpy(char *d, const char *s)
{
    while ((*d++ = *s++)) {
    }
}
int main(void)
{
    char buf[16];
    const char *src = "cgfried";
    int i;
    cpy(buf, src);
    for (i = 0; src[i] || buf[i]; i++)
        if (buf[i] != src[i])
            return 1;
    return 0;
}
