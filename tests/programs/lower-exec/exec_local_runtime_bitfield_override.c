// EXIT_CODE: 0
// Superseding one deferred bit-field store must not discard a neighboring
// deferred store merely because both fields share a storage byte.
static int first_calls;
static int neighbor_calls;

static unsigned int first_value(void)
{
    first_calls++;
    return 7;
}

static unsigned int neighbor_value(void)
{
    neighbor_calls++;
    return 5;
}

int main(void)
{
    struct Bits {
        unsigned int first : 3;
        unsigned int neighbor : 5;
    } bits = {
        .first = first_value(),
        .neighbor = neighbor_value(),
        .first = 2,
    };

    if (first_calls != 0)
        return 1;
    if (neighbor_calls != 1)
        return 2;
    if (bits.first != 2 || bits.neighbor != 5)
        return 3;
    return 0;
}
