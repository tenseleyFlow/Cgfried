// OPT_EQ: all
// The public stack-save token is an opaque C pointer: this fixture stores it
// in a local, allocates two dynamic regions, restores through the reloaded
// value, and proves that the next equal-sized allocation reuses the original
// stack position. Capturing the first address as an integer before restore
// avoids inspecting a pointer after its dynamic allocation's lifetime ends.

static volatile unsigned long addresses[2];
static volatile unsigned char observed;
static volatile unsigned long requested = 257;
static volatile unsigned long noise_requested = 33;

int main(void)
{
    unsigned long size = requested;
    void *saved = __builtin_stack_save();
    unsigned char *first = __builtin_alloca(size);
    unsigned char *noise = __builtin_alloca(noise_requested);
    unsigned char *second;

    first[0] = 5;
    first[256] = 7;
    noise[0] = 11;
    noise[32] = 13;
    observed = first[0] + first[256] + noise[0] + noise[32];
    addresses[0] = (unsigned long)first;

    __builtin_stack_restore(saved);
    second = __builtin_alloca(size);
    addresses[1] = (unsigned long)second;
    second[0] = 17;
    second[256] = 19;

    if (observed != 36)
        return 1;
    if (addresses[0] != addresses[1])
        return 2;
    if (second[0] != 17 || second[256] != 19)
        return 3;
    return 0;
}
