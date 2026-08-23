// EXIT_CODE: 0
// Positive-only enum bitfields follow gcc's implementation-defined unsigned
// representation even though Cgfried's compatible type for a small enum is
// int. Negative-valued enums remain signed bitfields.
enum Positive { POSITIVE_HIGH_BIT = 148 };
enum Negative { NEGATIVE_ONE = -1, NEGATIVE_ZERO = 0 };

struct Bits {
    enum Positive positive : 8;
    enum Negative negative : 8;
};

int main(void)
{
    struct Bits bits = {0};

    bits.positive = POSITIVE_HIGH_BIT;
    bits.negative = NEGATIVE_ONE;
    return bits.positive == POSITIVE_HIGH_BIT && bits.negative == NEGATIVE_ONE
               ? 0
               : 1;
}
