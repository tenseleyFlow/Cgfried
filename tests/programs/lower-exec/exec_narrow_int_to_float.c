// AArch64 SCVTF/UCVTF consume a W or X register.  Narrow integer inputs must
// be sign- or zero-extended before conversion, including values loaded from
// memory rather than already promoted by arithmetic.
// EXIT_CODE: 0
static volatile signed char signed_char_value = -7;
static volatile short signed_short_value = -12345;
static volatile unsigned char unsigned_char_value = 250;
static volatile unsigned short unsigned_short_value = 60000;

int main(void)
{
    float sc = (float)signed_char_value;
    float ss = (float)signed_short_value;
    double uc = (double)unsigned_char_value;
    double us = (double)unsigned_short_value;

    if (sc != -7.0f)
        return 1;
    if (ss != -12345.0f)
        return 2;
    if (uc != 250.0)
        return 3;
    if (us != 60000.0)
        return 4;
    return 0;
}
