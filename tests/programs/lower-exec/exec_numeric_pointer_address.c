// EXIT_CODE: 0
// Absolute integer-to-pointer constants remain numeric while member and index
// address arithmetic adds byte offsets.  The torture case 20050516-1.c
// exposed both failure modes: rejecting the initializer, and the older silent
// failure that emitted a zero pointer instead of the requested address.
struct Inner {
    char pad;
    int value;
};

struct Outer {
    char lead[8];
    struct Inner inner;
    int tail[4];
};

static int *const nested_member =
    &((struct Outer *const)0x4000)->inner.value;
static int *const direct_index = &((int *const)0x5000)[3];
static int *const member_index = &((struct Outer *const)0x6000)->tail[2];

int main(void)
{
    if ((unsigned long)nested_member != 0x400c)
        return 1;
    if ((unsigned long)direct_index != 0x500c)
        return 2;
    if ((unsigned long)member_index != 0x6018)
        return 3;
    return 0;
}
