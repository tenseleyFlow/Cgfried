// FLAGS: -fsyntax-only -Wextra
// WARN_COUNT: 0
/* The logical and relational operators yield int 0 or int 1 by construction
 * (6.5.3.3p5, 6.5.8p6, 6.5.9p3, 6.5.13p3, 6.5.14p3), so a signed operand that
 * IS one of those results cannot be negative and comparing it against an
 * unsigned type is not a sign confusion. gcc 8 and gcc 16 both stay silent.
 *
 * musl's fopencookie.c leans on it directly: `remain > !!f->buf_size`. */
int sign_compare_not(unsigned long remain, int n)
{
    return remain > !!n;
}

int sign_compare_relational(unsigned long remain, int a, int b)
{
    return remain > (a < b);
}
