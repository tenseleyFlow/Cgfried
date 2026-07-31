// Integer promotions: the sign surprises.
// EXIT_CODE: 0
int main(void)
{
    char c = -1;
    unsigned short us = 65535;
    if (!((c < 0u) == 0))
        return 1; /* c promotes to int -1; -1 < 0u is
                     UNSIGNED: false */
    if ((us + 1) != 65536)
        return 2; /* unsigned short promotes SIGNED */
    if ((c >> 1) != -1)
        return 3; /* promoted arithmetic shift */
    return 0;
}
