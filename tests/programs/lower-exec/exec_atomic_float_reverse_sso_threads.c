// Reverse-order floating updates must stay inside one atomic CAS loop.
// FLAGS: -lpthread
// OPT_EQ: -O0 -O2
// CHECK: OK
#define BE __attribute__((scalar_storage_order("big-endian")))

enum { THREADS = 4, INCREMENTS = 10000 };

struct Counters {
    _Atomic(float) f[1];
    _Atomic(double) d[1];
} BE;

static struct Counters counters;

static void *increment(void *unused)
{
    int i;

    (void)unused;
    for (i = 0; i < INCREMENTS; i++) {
        counters.f[0] += 1.0f;
        counters.d[0] += 1.0;
    }
    return 0;
}

/* Keep the GNU attribute visible before glibc's non-GNU compatibility
 * macros; pthread declarations are needed only by main below. */
#include <pthread.h>
#include <stdio.h>

int main(void)
{
    pthread_t threads[THREADS];
    const unsigned char *p = (const unsigned char *)&counters;
    int i;

    for (i = 0; i < THREADS; i++)
        if (pthread_create(&threads[i], 0, increment, 0) != 0)
            return 1;
    for (i = 0; i < THREADS; i++)
        if (pthread_join(threads[i], 0) != 0)
            return 2;
    if (counters.f[0] != 40000.0f || counters.d[0] != 40000.0)
        return 3;
    if (p[0] != 0x47 || p[1] != 0x1c || p[2] != 0x40 || p[3] != 0x00 ||
        p[8] != 0x40 || p[9] != 0xe3 || p[10] != 0x88 || p[11] != 0x00 ||
        p[12] != 0x00 || p[13] != 0x00 || p[14] != 0x00 || p[15] != 0x00)
        return 4;
    puts("OK");
    return 0;
}
