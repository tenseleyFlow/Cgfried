// OPT_EQ: all
// Literal edge bytes survive the numeric emission: \n(10) "(34) \\(92)
// \377(255).
// EXIT_CODE: 0
int main(void)
{
    const char *s = "\n\"\\\377";
    if (s[0] != 10)
        return 1;
    if (s[1] != 34)
        return 2;
    if (s[2] != 92)
        return 3;
    if ((unsigned char)s[3] != 255)
        return 4;
    if (s[4] != 0)
        return 5;
    return 0;
}
