// Integer promotions use a bit-field's effective precision rather than the
// rank of its GNU-permitted long or long long base. Explicit casts still
// create full-width values, and compound assignment uses the same promotion
// before converting its result back to the field.

struct widths {
    unsigned int u3 : 3;
    signed long s31 : 31;
    signed long s32 : 32;
    unsigned long u31 : 31;
    unsigned long u32 : 32;
    unsigned long long ull3 : 3;
    unsigned long long ull35 : 35;
};

struct modulo {
    signed int i : 7;
    unsigned int u : 7;
};

int main(void)
{
    struct widths w = {0};
    struct modulo m;
    struct {
        unsigned long long u : 3;
    } compound;
    int i = -13;
    unsigned int u = 61;
    unsigned int unsigned_result = -13U % 61;
    int signed_result = -13 % 61;

    if ((w.u3 - 2) >= 0 || (w.s31 - 2) >= 0 || (w.s32 - 2) >= 0 ||
        (w.u31 - 2) >= 0 || (w.ull3 - 2) >= 0)
        return 1;
    if ((w.u32 - 2) < 0 || (w.ull35 - 2) < 0)
        return 2;

    m.u = 61;
    m.i = -13;
    if (i % u != unsigned_result || i % (unsigned int)u != unsigned_result)
        return 3;
    if (i % m.u != signed_result || m.i % m.u != signed_result)
        return 4;
    if (i % (unsigned int)m.u != unsigned_result ||
        m.i % (unsigned int)m.u != unsigned_result)
        return 5;

    compound.u = 1;
    if ((compound.u /= -1) != 7 || compound.u != 7)
        return 6;
    return 0;
}
