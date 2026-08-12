// EXIT_CODE: 0
// Later designated initializers replace the selected union representation.
// Selecting a different union member clears the old member and any pointer
// relocation; subsequent designators within the same member accumulate.
int target;

union StringOverride {
    void *p;
    char s[8];
};

union BitsOverride {
    unsigned int word;
    struct {
        unsigned int low : 3;
        unsigned int high : 5;
    } bits;
};

static union StringOverride string_override = {.p = &target, .s = "A"};
static union BitsOverride zero_override = {
    .word = 0xffffffffu,
    .bits.low = 0,
};
static union BitsOverride accumulated = {
    .bits.low = 1,
    .bits.high = 2,
};

int main(void)
{
    if (string_override.s[0] != 'A' || string_override.s[1] != 0)
        return 1;
    if (zero_override.word != 0)
        return 2;
    if (accumulated.bits.low != 1 || accumulated.bits.high != 2)
        return 3;
    return 0;
}
