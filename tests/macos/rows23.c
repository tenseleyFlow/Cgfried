/* Row 2 (caller-side extension) and row 3 (natural-size stack packing),
   both proved by MIXED LINK: clang compiles the callees, which on Apple
   read their registers raw and their stack slots at packed offsets. */
int printf(const char *, ...);

int ext4(signed char a, unsigned char b, short c, unsigned short d);
long pack5(long a0, long a1, long a2, long a3, long a4, long a5, long a6,
           long a7, signed char p, unsigned char q, short r, unsigned short s,
           int t);

int main(void)
{
    int x = 0x1234ff80;

    printf("ext4=%d\n", ext4((signed char)x, (unsigned char)x, (short)x,
                             (unsigned short)x));
    printf("pack5=%ld\n", pack5(0, 1, 2, 3, 4, 5, 6, 7, (signed char)-1,
                                (unsigned char)250, (short)-3,
                                (unsigned short)70000u, 9));
    return 0;
}
