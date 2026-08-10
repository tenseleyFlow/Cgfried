// OPT_EQ: all

#include <stdint.h>

#ifndef REPS
#define REPS 10000
#endif

struct node {
    struct node *next;
    uint32_t value;
};
static volatile uint32_t sink;

__attribute__((noinline)) uint32_t kernel_run(void)
{
    static struct node nodes[257];
    struct node *p;
    uint32_t sum = 0;
    unsigned i;
    int r;

    for (i = 0; i < 257; ++i) {
        nodes[i].next = &nodes[(i + 67u) % 257u];
        nodes[i].value = i;
    }
    p = &nodes[0];
    for (r = 0; r < REPS; ++r) {
        sum = 0;
        for (i = 0; i < 257; ++i) {
            p = p->next;
            sum += p->value;
        }
    }
    return sum;
}

int main(void)
{
    uint32_t got = kernel_run();
    sink = got;
    return got != UINT32_C(32896);
}
