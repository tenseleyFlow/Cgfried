// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: '__alignof__' applied to a bit-field

struct bits {
    unsigned value : 3;
};

int alignment(struct bits b)
{
    return __alignof__(b.value);
}
