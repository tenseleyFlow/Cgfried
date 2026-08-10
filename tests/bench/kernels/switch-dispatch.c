// OPT_EQ: all

#include <stdint.h>

#ifndef REPS
#define REPS 100000
#endif

static volatile uint32_t sink;

__attribute__((noinline)) uint32_t kernel_run(void)
{
    uint32_t value = 0;
    unsigned i;
    int r;

    for (r = 0; r < REPS; ++r) {
        value = 0;
        for (i = 0; i < 128; ++i)
            switch ((i * 13u + 5u) & 15u) {
            case 0:
                value += 3;
                break;
            case 1:
                value ^= 0x11;
                break;
            case 2:
                value += 7;
                break;
            case 3:
                value ^= 0x33;
                break;
            case 4:
                value += 11;
                break;
            case 5:
                value ^= 0x55;
                break;
            case 6:
                value += 13;
                break;
            case 7:
                value ^= 0x77;
                break;
            case 8:
                value += 17;
                break;
            case 9:
                value ^= 0x99;
                break;
            case 10:
                value += 19;
                break;
            case 11:
                value ^= 0xbb;
                break;
            case 12:
                value += 23;
                break;
            case 13:
                value ^= 0xdd;
                break;
            case 14:
                value += 29;
                break;
            default:
                value ^= 0xff;
                break;
            }
    }
    return value;
}

int main(void)
{
    uint32_t got = kernel_run();
    sink = got;
    return got != UINT32_C(848);
}
