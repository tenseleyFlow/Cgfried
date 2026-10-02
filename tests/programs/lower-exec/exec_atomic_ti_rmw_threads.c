// Contention proof: a split load/store implementation loses increments here.
// FLAGS: -lpthread
// OPT_EQ: -O0 -O2
// CHECK: OK
#include <pthread.h>
#include <stdio.h>

typedef unsigned __int128 u128;

enum { THREADS = 4, INCREMENTS = 10000 };

static _Atomic(u128) counter;

static void *increment(void *unused)
{
    int i;

    (void)unused;
    for (i = 0; i < INCREMENTS; i++)
        counter++;
    return 0;
}

int main(void)
{
    pthread_t threads[THREADS];
    int i;

    for (i = 0; i < THREADS; i++)
        if (pthread_create(&threads[i], 0, increment, 0) != 0)
            return 1;
    for (i = 0; i < THREADS; i++)
        if (pthread_join(threads[i], 0) != 0)
            return 2;
    if (counter != (u128)THREADS * INCREMENTS)
        return 3;
    puts("OK");
    return 0;
}
