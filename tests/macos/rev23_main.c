#include <stdio.h>
int ext4x(signed char a, unsigned char b, short c, unsigned short d);
long pack5x(long a0, long a1, long a2, long a3, long a4, long a5, long a6,
            long a7, signed char p, unsigned char q, short r, unsigned short s,
            int t);
int main(void)
{
    int x = 0x1234ff80;

    printf("ext4x=%d\n", ext4x((signed char)x, (unsigned char)x, (short)x,
                               (unsigned short)x));
    printf("pack5x=%ld\n", pack5x(0, 1, 2, 3, 4, 5, 6, 7, (signed char)-1,
                                  (unsigned char)250, (short)-3,
                                  (unsigned short)70000u, 9));
    return 0;
}
