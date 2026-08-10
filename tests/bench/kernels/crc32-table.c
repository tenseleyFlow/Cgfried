// OPT_EQ: all

#include <stddef.h>
#include <stdint.h>

#ifndef REPS
#define REPS 100000
#endif

static volatile uint32_t sink;

__attribute__((noinline)) uint32_t kernel_run(void)
{
    uint32_t table[256];
    static const unsigned char data[] = "123456789";
    uint32_t crc = 0;
    unsigned i, j;
    int r;

    for (i = 0; i < 256; ++i) {
        uint32_t v = i;
        for (j = 0; j < 8; ++j)
            v = (v >> 1) ^ (UINT32_C(0xedb88320) & (0u - (v & 1u)));
        table[i] = v;
    }
    for (r = 0; r < REPS; ++r) {
        crc = UINT32_C(0xffffffff);
        for (i = 0; i + 1 < sizeof(data); ++i)
            crc = table[(crc ^ data[i]) & 255u] ^ (crc >> 8);
        crc ^= UINT32_C(0xffffffff);
    }
    return crc;
}

int main(void)
{
    uint32_t got = kernel_run();
    sink = got;
    return got != UINT32_C(0xcbf43926);
}
