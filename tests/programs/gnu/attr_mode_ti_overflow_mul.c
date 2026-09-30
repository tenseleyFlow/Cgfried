// GNU checked multiplication uses infinite-precision signed arithmetic even
// when either independently typed operand is mode(TI).
typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((mode(TI)));

#define UONE ((u128)1)
#define UBIT(N) (UONE << (N))
#define UMAX (~(u128)0)
#define IMAX ((i128)(UBIT(127) - UONE))
#define IMIN (-IMAX - 1)

_Static_assert(__builtin_mul_overflow_p(UMAX, (u128)2, (u128)0),
               "unsigned TI product overflow");
_Static_assert(!__builtin_mul_overflow_p(UMAX, UONE, (u128)0),
               "unsigned TI product edge");
_Static_assert(__builtin_mul_overflow_p(IMIN, (i128)-1, (i128)0),
               "signed TI minimum overflow");
_Static_assert(!__builtin_mul_overflow_p(IMIN, (i128)1, (i128)0),
               "signed TI minimum edge");
_Static_assert(!__builtin_mul_overflow_p(UBIT(64), UBIT(63), (u128)0),
               "unsigned TI bit 127 product");
_Static_assert(__builtin_mul_overflow_p(UBIT(64), UBIT(64), (u128)0),
               "unsigned TI bit 128 product");
_Static_assert(__builtin_mul_overflow_p((i128)-1, (i128)1, (u128)0),
               "negative product unsigned destination");
_Static_assert(__builtin_mul_overflow_p(UBIT(100), (u128)3,
                                        (unsigned long long)0),
               "wide operands narrow selector");

static volatile u128 volatile_unsigned;
static volatile i128 volatile_signed;
static volatile u128 volatile_result;
static u128 staged;
static u128 result;
static int calls;

static int expect(int condition, int code)
{
    return condition ? 0 : code;
}

static u128 left_once(void)
{
    calls++;
    return staged;
}

static u128 mutate_right(void)
{
    calls++;
    staged = 9;
    return (u128)2;
}

static u128 *mutating_result(void)
{
    calls++;
    staged = 12;
    return &result;
}

int main(void)
{
    u128 unsigned_result;
    i128 signed_result;
    unsigned long long narrow_unsigned;
    long long narrow_signed;
    int failure;
    _Bool overflow;

    overflow = __builtin_mul_overflow(UMAX, (u128)2, &unsigned_result);
    failure = expect(overflow && unsigned_result == UMAX - UONE, 1);
    if (failure)
        return failure;

    overflow = __builtin_mul_overflow(UBIT(64), UBIT(63), &unsigned_result);
    failure = expect(!overflow && unsigned_result == UBIT(127), 2);
    if (failure)
        return failure;

    overflow = __builtin_mul_overflow(UBIT(64), UBIT(64), &unsigned_result);
    failure = expect(overflow && unsigned_result == 0, 3);
    if (failure)
        return failure;

    overflow = __builtin_mul_overflow(IMIN, (i128)-1, &signed_result);
    failure = expect(overflow && signed_result == IMIN, 4);
    if (failure)
        return failure;

    overflow = __builtin_mul_overflow(IMIN, (i128)1, &signed_result);
    failure = expect(!overflow && signed_result == IMIN, 5);
    if (failure)
        return failure;

    overflow = __builtin_mul_overflow((i128)-3, (i128)7, &signed_result);
    failure = expect(!overflow && signed_result == -21, 6);
    if (failure)
        return failure;

    overflow = __builtin_mul_overflow((i128)-1, (i128)1, &unsigned_result);
    failure = expect(overflow && unsigned_result == UMAX, 7);
    if (failure)
        return failure;

    overflow = __builtin_mul_overflow((u128)0, IMIN, &signed_result);
    failure = expect(!overflow && signed_result == 0, 8);
    if (failure)
        return failure;

    overflow =
        __builtin_mul_overflow(UBIT(100) + (u128)7, (u128)3, &narrow_unsigned);
    failure = expect(overflow && narrow_unsigned == 21, 9);
    if (failure)
        return failure;

    overflow = __builtin_mul_overflow(-(i128)(UBIT(100) + (u128)7), (i128)3,
                                      &narrow_signed);
    failure = expect(overflow && narrow_signed == -21, 10);
    if (failure)
        return failure;

    unsigned_result = UBIT(127);
    overflow =
        __builtin_mul_overflow(unsigned_result, (u128)2, &unsigned_result);
    failure = expect(overflow && unsigned_result == 0, 11);
    if (failure)
        return failure;

    overflow = __builtin_mul_overflow((u128)2, UMAX, &unsigned_result);
    failure = expect(overflow && unsigned_result == UMAX - UONE, 12);
    if (failure)
        return failure;

    volatile_unsigned = UMAX;
    overflow =
        __builtin_mul_overflow(volatile_unsigned, (u128)2, &volatile_result);
    failure = expect(overflow && volatile_result == UMAX - UONE, 13);
    if (failure)
        return failure;

    volatile_signed = IMIN;
    overflow =
        __builtin_mul_overflow(volatile_signed, (i128)-1, &signed_result);
    failure = expect(overflow && signed_result == IMIN, 14);
    if (failure)
        return failure;

    staged = 5;
    calls = 0;
    overflow = __builtin_mul_overflow(left_once(), mutate_right(), &result);
    failure =
        expect(!overflow && result == 10 && staged == 9 && calls == 2, 15);
    if (failure)
        return failure;

    staged = 7;
    calls = 0;
    overflow = __builtin_mul_overflow(left_once(), (u128)2, mutating_result());
    failure =
        expect(!overflow && result == 14 && staged == 12 && calls == 2, 16);
    if (failure)
        return failure;

    staged = UBIT(100);
    calls = 0;
    overflow = __builtin_mul_overflow_p(left_once(), mutate_right(),
                                        (unsigned long long)0);
    failure = expect(overflow && staged == 9 && calls == 2, 17);
    if (failure)
        return failure;

    return 0;
}
