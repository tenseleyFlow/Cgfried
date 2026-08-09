// FLAGS: -fsyntax-only -Wextra
// WARN_COUNT: 0
/* An unsigned type NARROWER than int promotes to a signed int that can never
 * hold a negative value, so comparing it against an unsigned type is not a
 * sign confusion. gcc 8 and gcc 16 both stay silent here; measured, not
 * assumed.
 *
 * musl's intscan.c is the reason this exists: `val[c] >= base` with an
 * unsigned char through a pointer and an unsigned base, seven times in one
 * file, and it was the largest false-positive cluster the zero-false-positive
 * musl gate reported the day extended asm made the file parse. */
int sign_compare_narrow_unsigned(unsigned char c, unsigned base)
{
    return c >= base;
}

int sign_compare_narrow_ushort(unsigned short v, unsigned long limit)
{
    return v < limit;
}
