// OPT_EQ: all

#include <stddef.h>
#include <stdint.h>

#ifndef REPS
#define REPS 100000
#endif

static volatile uint32_t sink;

__attribute__((noinline)) uint32_t kernel_run(void)
{
    static const unsigned char input[] = "ab12,+xy-99;(q7)*42/z_0!";
    uint32_t score = 0;
    int r;
    size_t i;

    for (r = 0; r < REPS; ++r) {
        score = 0;
        for (i = 0; i + 1 < sizeof(input); ++i) {
            unsigned char ch = input[i];
            switch (ch) {
            case '+':
            case '-':
            case '*':
            case '/':
                score += 17;
                break;
            case '(':
            case ')':
            case ';':
            case ',':
                score ^= 0x55;
                break;
            default:
                if (ch >= '0' && ch <= '9')
                    score += ch - '0';
                else if ((ch >= 'a' && ch <= 'z') || ch == '_')
                    score = score * 3u + ch;
                else
                    score ^= ch;
            }
        }
    }
    return score;
}

int main(void)
{
    uint32_t got = kernel_run();
    sink = got;
    return got != UINT32_C(136313);
}
