/* Reverse direction: OUR callee, clang's caller. Our callee widens on read
 * regardless of target, which is correct under both ABIs. */
signed char rc_ext(signed char a, unsigned char b, short c, unsigned short d);
long rc_pack(long a0, long a1, long a2, long a3, long a4, long a5, long a6,
             long a7, signed char p, unsigned char q, short r,
             unsigned short s, int t);
int ext4x(signed char a, unsigned char b, short c, unsigned short d)
{
    return a + b + c + d;
}
long pack5x(long a0, long a1, long a2, long a3, long a4, long a5, long a6,
            long a7, signed char p, unsigned char q, short r, unsigned short s,
            int t)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    (void)a7;
    return (long)p * 1 + (long)q * 10 + (long)r * 100 + (long)s * 1000 +
           (long)t * 10000;
}
