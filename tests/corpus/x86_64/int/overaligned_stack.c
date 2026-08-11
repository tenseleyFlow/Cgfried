// OPT_EQ: all
// Fixed and dynamic automatic objects stronger than the ABI stack alignment.
// The VLA loop also proves that calls cannot overlap the object and that each
// scope restores the stack before its next iteration.
#include <stdio.h>

__attribute__((noinline)) static long sum10(long a, long b, long c, long d,
                                            long e, long f, long g, long h,
                                            long i, long j)
{
    return a + b + c + d + e + f + g + h + i + j;
}

static int fixed_object(void)
{
    unsigned char left = 7;
    _Alignas(64) unsigned char object[5] = {1, 2, 3, 4, 5};
    unsigned char right = 9;

    return ((unsigned long)object & 63) == 0 && left == 7 && right == 9 &&
           object[0] == 1 && object[4] == 5;
}

static int dynamic_objects(int seed)
{
    int round;

    for (round = 0; round < 24; round++) {
        int n = seed + round;
        _Alignas(64) unsigned char object[n];
        long call_result;

        object[0] = (unsigned char)(round + 1);
        object[n - 1] = (unsigned char)(round + 31);
        call_result = sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
        if (((unsigned long)object & 63) != 0 || call_result != 55 ||
            object[0] != (unsigned char)(round + 1) ||
            object[n - 1] != (unsigned char)(round + 31))
            return 0;
    }
    return 1;
}

int main(void)
{
    if (!fixed_object() || !dynamic_objects(17))
        return 1;
    puts("OK");
    return 0;
}
