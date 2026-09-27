// OPT_EQ: all
// EXIT_CODE: 0
/* Required mode(TI) constant expressions are evaluated as exact two-limb
 * values.  Exercise every integer operator class at and across the limb
 * boundary; the runtime checks also pin the emitted static images. */
typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((mode(TI)));

#define UONE ((u128)1)
#define UBIT(N) (UONE << (N))
#define IMIN ((i128)UBIT(127))
#define IMAX ((i128)(UBIT(127) - UONE))

_Static_assert(+UBIT(90) == UBIT(90), "unary plus");
_Static_assert(-UONE == (u128)-1, "unsigned unary minus");
_Static_assert(~(u128)0 == (u128)-1, "wide complement");
_Static_assert((!UBIT(90)) == 0 && (!(u128)0) == 1, "wide logical not");

_Static_assert((UBIT(64) - UONE) + UONE == UBIT(64), "add carry");
_Static_assert(UBIT(64) - UONE == (u128)(unsigned long)-1, "sub borrow");
_Static_assert(UBIT(70) * (UBIT(40) + 3) ==
                   UBIT(110) + UBIT(70) * 3,
               "wide multiply");
_Static_assert((i128)UBIT(70) * -5 == -((i128)UBIT(70) * 5),
               "signed multiply");
_Static_assert((i128)2 * (i128)UBIT(100) == (i128)UBIT(101),
               "signed multiply by high limb");
_Static_assert((u128)-1 * 2 == (u128)-2, "unsigned multiply wraps");
_Static_assert((UBIT(100) + 123) / UBIT(60) == UBIT(40),
               "wide quotient");
_Static_assert((UBIT(100) + 123) % UBIT(60) == 123, "wide remainder");
_Static_assert((UBIT(110) + UBIT(70) * 3) / (UBIT(40) + 3) == UBIT(70),
               "wide divisor");
_Static_assert((UBIT(100) + UBIT(30)) / UBIT(80) == UBIT(20),
               "high-limb divisor");
_Static_assert((UBIT(100) + UBIT(30)) % UBIT(80) == UBIT(30),
               "high-limb remainder");
_Static_assert((u128)-1 / UBIT(127) == 1, "top-bit divisor");
_Static_assert((u128)-1 % UBIT(127) == UBIT(127) - 1,
               "top-bit remainder");

_Static_assert((UBIT(67) | UBIT(7)) == UBIT(67) + 128, "wide or");
_Static_assert(((u128)-1 & UBIT(96)) == UBIT(96), "wide and");
_Static_assert((UBIT(99) ^ UBIT(99)) == 0, "wide xor");
_Static_assert((UBIT(100) >> 36) == UBIT(64), "logical right shift");
_Static_assert((UBIT(127) >> 64) == UBIT(63), "whole-limb shift");
_Static_assert(((i128)-9 >> 2) == -3, "arithmetic right shift");

_Static_assert(UBIT(127) > UBIT(126), "unsigned comparison");
_Static_assert(IMIN < (i128)-1 && IMAX > (i128)0, "signed comparison");
_Static_assert(!((i128)-1 < UONE), "usual signed/unsigned conversion");
_Static_assert(IMIN + 1 < (i128)0 && IMAX - 1 > (i128)0,
               "signed add and subtract");
_Static_assert(UBIT(100) != UBIT(99) && UBIT(100) == UBIT(100),
               "wide equality");
_Static_assert(UBIT(100) && 1, "wide logical and");
_Static_assert(UBIT(100) || (1 / 0), "wide short circuit");
_Static_assert((UBIT(100) ? 7 : (1 / 0)) == 7, "wide condition");
_Static_assert((UBIT(100) ?: 3) == UBIT(100), "GNU omitted condition");

_Static_assert(-((i128)(UBIT(100) + 9)) / (i128)UBIT(50) ==
                   -((i128)UBIT(50)),
               "signed quotient");
_Static_assert(-((i128)(UBIT(100) + 9)) % (i128)UBIT(50) == -9,
               "signed remainder");

static u128 folded_product = UBIT(110) + UBIT(70) * 3;
static u128 folded_remainder = (UBIT(100) + 123) % UBIT(60);
static i128 folded_signed = -((i128)(UBIT(100) + 9)) / (i128)UBIT(50);

int main(void)
{
    if (folded_product != UBIT(110) + UBIT(70) * 3)
        return 1;
    if (folded_remainder != 123)
        return 2;
    if (folded_signed != -((i128)UBIT(50)))
        return 3;
    return 0;
}
