// GNU checked-overflow addition and subtraction use infinite-precision signed
// arithmetic even when either independently typed operand is mode(TI).
typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((mode(TI)));

#define UONE ((u128)1)
#define UBIT(N) (UONE << (N))
#define UMAX (~(u128)0)
#define IMAX ((i128)(UBIT(127) - UONE))
#define IMIN (-IMAX - 1)

_Static_assert(__builtin_add_overflow_p(UMAX, UONE, (u128)0),
               "unsigned TI carry");
_Static_assert(!__builtin_add_overflow_p(UMAX - UONE, UONE, (u128)0),
               "unsigned TI edge");
_Static_assert(__builtin_add_overflow_p(IMAX, UONE, (i128)0),
               "signed TI positive edge");
_Static_assert(__builtin_sub_overflow_p(IMIN, UONE, (i128)0),
               "signed TI negative edge");
_Static_assert(!__builtin_sub_overflow_p(IMIN, IMIN, (i128)0),
               "signed TI cancellation");
_Static_assert(__builtin_sub_overflow_p((u128)0, UONE, (u128)0),
               "unsigned TI negative result");
_Static_assert(__builtin_add_overflow_p(UBIT(100), UBIT(100),
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
    return UONE;
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

    overflow = __builtin_add_overflow(UMAX, UONE, &unsigned_result);
    failure = expect(overflow && unsigned_result == 0, 1);
    if (failure)
        return failure;

    overflow = __builtin_add_overflow(UBIT(127), UBIT(127), &unsigned_result);
    failure = expect(overflow && unsigned_result == 0, 2);
    if (failure)
        return failure;

    overflow = __builtin_add_overflow(UBIT(126), UBIT(126), &unsigned_result);
    failure = expect(!overflow && unsigned_result == UBIT(127), 3);
    if (failure)
        return failure;

    overflow = __builtin_add_overflow(IMAX, UONE, &signed_result);
    failure = expect(overflow && signed_result == IMIN, 4);
    if (failure)
        return failure;

    overflow = __builtin_sub_overflow(IMIN, UONE, &signed_result);
    failure = expect(overflow && signed_result == IMAX, 5);
    if (failure)
        return failure;

    overflow = __builtin_add_overflow(IMIN, IMAX, &signed_result);
    failure = expect(!overflow && signed_result == -1, 6);
    if (failure)
        return failure;

    overflow = __builtin_sub_overflow((i128)-5, (i128)-7, &signed_result);
    failure = expect(!overflow && signed_result == 2, 7);
    if (failure)
        return failure;

    overflow = __builtin_add_overflow(UBIT(100), (u128)7, &narrow_unsigned);
    failure = expect(overflow && narrow_unsigned == 7, 8);
    if (failure)
        return failure;

    overflow = __builtin_sub_overflow(IMIN, UONE, &narrow_signed);
    failure = expect(overflow && narrow_signed == -1, 9);
    if (failure)
        return failure;

    unsigned_result = UMAX;
    overflow = __builtin_add_overflow(unsigned_result, UONE, &unsigned_result);
    failure = expect(overflow && unsigned_result == 0, 10);
    if (failure)
        return failure;

    volatile_unsigned = UMAX;
    overflow =
        __builtin_add_overflow(volatile_unsigned, UONE, &volatile_result);
    failure = expect(overflow && volatile_result == 0, 11);
    if (failure)
        return failure;

    volatile_signed = IMIN;
    overflow = __builtin_sub_overflow(volatile_signed, UONE, &signed_result);
    failure = expect(overflow && signed_result == IMAX, 12);
    if (failure)
        return failure;

    staged = 5;
    calls = 0;
    overflow = __builtin_add_overflow(left_once(), mutate_right(), &result);
    failure = expect(!overflow && result == 6 && staged == 9 && calls == 2, 13);
    if (failure)
        return failure;

    staged = 7;
    calls = 0;
    overflow = __builtin_sub_overflow(left_once(), UONE, mutating_result());
    failure =
        expect(!overflow && result == 6 && staged == 12 && calls == 2, 14);
    if (failure)
        return failure;

    staged = UBIT(100);
    calls = 0;
    overflow = __builtin_add_overflow_p(left_once(), mutate_right(),
                                        (unsigned long long)0);
    failure = expect(overflow && staged == 9 && calls == 2, 15);
    if (failure)
        return failure;

    return 0;
}
