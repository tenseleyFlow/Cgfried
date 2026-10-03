// A representation transform must stay inside one atomic TI CAS operation.
// FLAGS: -lpthread
// OPT_EQ: -O0 -O2
// CHECK: OK
#include <pthread.h>
#include <stdio.h>

typedef unsigned __int128 u128;

#define BE __attribute__((scalar_storage_order("big-endian")))

enum { THREADS = 4, INCREMENTS = 10000 };

struct Counter {
    _Atomic(u128) value;
} BE;

static struct Counter counter;

static void *increment(void *unused)
{
    int i;

    (void)unused;
    for (i = 0; i < INCREMENTS; i++)
        counter.value++;
    return 0;
}

static int physical_equals(u128 logical)
{
    const unsigned char *bytes = (const unsigned char *)(const void *)&counter;
    unsigned i;

    for (i = 0; i < 16; i++)
        if (bytes[i] != (unsigned char)(logical >> ((15 - i) * 8)))
            return 0;
    return 1;
}

int main(void)
{
    pthread_t threads[THREADS];
    u128 want = (u128)THREADS * INCREMENTS;
    int i;

    for (i = 0; i < THREADS; i++)
        if (pthread_create(&threads[i], 0, increment, 0) != 0)
            return 1;
    for (i = 0; i < THREADS; i++)
        if (pthread_join(threads[i], 0) != 0)
            return 2;
    if (counter.value != want)
        return 3;
    if (!physical_equals(want))
        return 4;
    puts("OK");
    return 0;
}
