// Reverse-order binary128 updates must stay inside one atomic CAS loop.
// FLAGS: -lpthread
// OPT_EQ: -O0 -O2
// CHECK: OK
#define BE __attribute__((scalar_storage_order("big-endian")))

enum { THREADS = 4, INCREMENTS = 1000 };

struct Counter {
    _Atomic(_Float128) value;
} BE;

static struct Counter counter;

static void *increment(void *unused)
{
    int i;

    (void)unused;
    for (i = 0; i < INCREMENTS; i++)
        counter.value += 1.0F128;
    return 0;
}

/* Keep the GNU attribute visible before glibc's non-GNU compatibility
 * macros; pthread declarations are needed only by main below. */
#include <pthread.h>
#include <stdio.h>

int main(void)
{
    pthread_t threads[THREADS];
    const unsigned char *p = (const unsigned char *)&counter;
    int i;

    for (i = 0; i < THREADS; i++)
        if (pthread_create(&threads[i], 0, increment, 0) != 0)
            return 1;
    for (i = 0; i < THREADS; i++)
        if (pthread_join(threads[i], 0) != 0)
            return 2;
    if (counter.value != 4000.0F128)
        return 3;
    if (p[0] != 0x40 || p[1] != 0x0a || p[2] != 0xf4)
        return 4;
    puts("OK");
    return 0;
}
