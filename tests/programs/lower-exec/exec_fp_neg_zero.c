// -0.0: oeq to +0.0, but the sign survives the xor-mask negate.
// EXIT_CODE: 0
int main(void)
{
    volatile double z = 0.0;
    double nz = -z;
    if (nz != 0.0)
        return 1; /* -0.0 == 0.0 */
    if (1.0 / nz > 0.0)
        return 2; /* but the sign is real */
    return 0;
}
