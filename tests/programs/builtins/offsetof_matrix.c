// offsetof over every shape the folder must walk: plain member, a
// nested named struct, an ANONYMOUS member (6.7.2.1p13 transparency,
// where the enclosing member's own offset must be accumulated), and
// both array-designator forms. Values gcc-verified.
// EXIT_CODE: 0
#include <stddef.h>
struct Inner {
    char pad;
    int deep;
};
struct S {
    int a;
    double b;
    char c[7];
    struct Inner in;
    struct {
        int anon_x;
        long anon_y;
    };
    int arr[4][3];
};

/* musl uses this pre-__builtin spelling in public headers.  It must fold at
 * file scope, including the arrow member offset and same-base pointer
 * subtraction, or the typedef is incorrectly classified as a VLA. */
#define LEGACY_OFFSETOF(type, member)                                      \
    ((unsigned long)((char *)&(((type *)0)->member) - (char *)0))
enum { LEGACY_OFFSET_VALUE = LEGACY_OFFSETOF(struct S, b) };
enum {
    LEGACY_INDEX_ZERO = LEGACY_OFFSETOF(struct S, arr[0]),
    LEGACY_INDEX_NESTED = LEGACY_OFFSETOF(struct S, arr[2][1])
};
typedef char legacy_offset_is_ice[LEGACY_OFFSETOF(struct S, b) == 8 ? 1 : -1];
typedef char legacy_index_is_ice[LEGACY_OFFSETOF(struct S, arr[2][1]) == 76
                                     ? 1
                                     : -1];

int main(void)
{
    if (offsetof(struct S, a) != 0)
        return 1;
    if (offsetof(struct S, b) != 8)
        return 2;
    if (offsetof(struct S, c) != 16)
        return 3;
    if (offsetof(struct S, in.deep) != 28)
        return 4;
    if (offsetof(struct S, anon_x) != 32)
        return 5;
    if (offsetof(struct S, anon_y) != 40)
        return 6;
    if (offsetof(struct S, arr[2]) != 72)
        return 7;
    if (offsetof(struct S, arr[2][1]) != 76)
        return 8;
    if (LEGACY_OFFSET_VALUE != 8 || LEGACY_OFFSETOF(struct S, b) != 8)
        return 10;
    if (LEGACY_INDEX_ZERO != 48 || LEGACY_INDEX_NESTED != 76)
        return 11;
    /* An ICE in every sense: usable as an array bound. */
    {
        static int ice[offsetof(struct S, b) == 8 ? 3 : -1];
        if (sizeof ice != 3 * sizeof(int))
            return 9;
    }
    return 0;
}
