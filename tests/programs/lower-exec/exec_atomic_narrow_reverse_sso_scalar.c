// Cgfried deliberately supports the direct scalar form that GCC rejects
// through its reverse-order address-taking path, while preserving _Atomic.
// EXIT_CODE: 0
// OPT_EQ: -O0 -O1 -O2 -O3 -Os
#define BE __attribute__((scalar_storage_order("big-endian")))

struct Counter {
    _Atomic(unsigned) value;
} BE;

static struct Counter counter = {0x01020304};

int main(void)
{
    const unsigned char *bytes = (const unsigned char *)&counter;
    unsigned old;

    if (counter.value != 0x01020304 || bytes[0] != 1 || bytes[1] != 2 ||
        bytes[2] != 3 || bytes[3] != 4)
        return 1;
    counter.value += 2;
    old = counter.value++;
    if (old != 0x01020306 || counter.value != 0x01020307)
        return 2;
    if (bytes[0] != 1 || bytes[1] != 2 || bytes[2] != 3 || bytes[3] != 7)
        return 3;
    return 0;
}
