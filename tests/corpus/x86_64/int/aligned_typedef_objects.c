// OPT_EQ: all
// GNU `aligned` on a typedef is a property of that TYPE. It survives aliases,
// controls every object's placement, and does not mutate the underlying tag.
#define A(n) __attribute__((aligned(n)))

typedef struct Tagged {
    char bytes[8];
} V A(8);
typedef V Alias;

V global_v;
static Alias static_v;

struct Holder {
    char lead;
    Alias value;
    char tail;
};

/* GCC's type-layer rule is exact, unlike record/member aligned: a typedef may
 * reduce alignment without changing size. This also pins under-aligned scalar
 * IR rather than validating only the original over-aligned torture shape. */
typedef long long Low A(1);
struct Tight {
    char lead;
    Low value;
    char tail;
};

static int local_static_is_aligned(void)
{
    static Alias value;

    return ((unsigned long)&value & 7u) == 0;
}

int main(void)
{
    Alias automatic;
    Alias array[2];
    struct Holder holder;
    struct Tight tight;

    _Static_assert(sizeof(V) == 8 && _Alignof(V) == 8, "typedef layout");
    _Static_assert(sizeof(struct Tagged) == 8 &&
                       _Alignof(struct Tagged) == 1,
                   "the named tag is unchanged");
    _Static_assert(sizeof(struct Holder) == 24 &&
                       _Alignof(struct Holder) == 8,
                   "containing layout");
    _Static_assert(__builtin_offsetof(struct Holder, value) == 8,
                   "member placement");
    _Static_assert(sizeof(Low) == 8 && _Alignof(Low) == 1,
                   "exact reduced type alignment");
    _Static_assert(sizeof(struct Tight) == 10 &&
                       _Alignof(struct Tight) == 1,
                   "reduced member layout");
    _Static_assert(__builtin_offsetof(struct Tight, value) == 1,
                   "reduced member placement");

    if (((unsigned long)&global_v & 7u) != 0)
        return 1;
    if (((unsigned long)&static_v & 7u) != 0)
        return 2;
    if (((unsigned long)&automatic & 7u) != 0)
        return 3;
    if (((unsigned long)&array[0] & 7u) != 0 ||
        ((unsigned long)&array[1] & 7u) != 0)
        return 4;
    if (((unsigned long)&holder.value & 7u) != 0)
        return 5;
    if (!local_static_is_aligned())
        return 6;

    tight.value = 0x1122334455667788LL;
    if (tight.value != 0x1122334455667788LL)
        return 7;
    return 0;
}
