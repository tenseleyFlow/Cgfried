/* The single-threaded half proves the ll/sc loops compute the right value
 * and return the OLD one. The threaded half proves the loop is actually
 * atomic — though under qemu-user the scheduling is tame enough that a
 * broken sequence can still pass, so this is a smoke test there and a real
 * one on hardware. */
#include <pthread.h>
#include <stdio.h>

int rmw_add(int *p, int v);
int rmw_sub(int *p, int v);
long rmw_and64(long *p, long v);
int rmw_xor(int *p, int v);
int rmw_xchg(int *p, int v);
int cas(int *p, int expected, int desired);
long seq_load(long *p);
int seq_store(int *p, int v);

static int fails;

static void chk(const char *w, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: %ld vs %ld\n", w, got, want);
        fails++;
    }
}

static void single_threaded(void)
{
    static const int vals[] = {0, 1, -1, 7, -7, 65535, -65536, 2147483647};
    unsigned i, j;

    for (i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
        for (j = 0; j < sizeof(vals) / sizeof(vals[0]); j++) {
            int cell = vals[i];
            long wide = vals[i];

            /* every RMW returns the PREVIOUS value, then leaves the result */
            chk("add.old", rmw_add(&cell, vals[j]), vals[i]);
            chk("add.new", cell, (int)((unsigned)vals[i] + (unsigned)vals[j]));

            cell = vals[i];
            chk("sub.old", rmw_sub(&cell, vals[j]), vals[i]);
            chk("sub.new", cell, (int)((unsigned)vals[i] - (unsigned)vals[j]));

            cell = vals[i];
            chk("xor.old", rmw_xor(&cell, vals[j]), vals[i]);
            chk("xor.new", cell, vals[i] ^ vals[j]);

            cell = vals[i];
            chk("xchg.old", rmw_xchg(&cell, vals[j]), vals[i]);
            chk("xchg.new", cell, vals[j]);

            wide = vals[i];
            chk("and64.old", rmw_and64(&wide, vals[j]), vals[i]);
            chk("and64.new", wide, (long)vals[i] & (long)vals[j]);

            /* cmpxchg swaps only on a match, and always returns the old */
            cell = vals[i];
            chk("cas.hit.old", cas(&cell, vals[i], vals[j]), vals[i]);
            chk("cas.hit.new", cell, vals[j]);

            cell = vals[i];
            if (vals[j] != vals[i]) {
                chk("cas.miss.old", cas(&cell, vals[j], 12345), vals[i]);
                chk("cas.miss.new", cell, vals[i]);
            }

            wide = vals[i];
            chk("seq_load", seq_load(&wide), vals[i]);
            cell = 0;
            seq_store(&cell, vals[j]);
            chk("seq_store", cell, vals[j]);
        }
}

#define THREADS 4
#define ITERS 20000

static int counter;
static int cas_counter;

static void *hammer(void *unused)
{
    int i;

    (void)unused;
    for (i = 0; i < ITERS; i++) {
        rmw_add(&counter, 1);
        /* the compare-exchange retry loop, driven until it wins */
        for (;;) {
            int seen = cas_counter;

            if (cas(&cas_counter, seen, seen + 1) == seen)
                break;
        }
    }
    return 0;
}

static void threaded(void)
{
    pthread_t th[THREADS];
    int i;

    for (i = 0; i < THREADS; i++)
        if (pthread_create(&th[i], 0, hammer, 0) != 0) {
            printf("FAIL pthread_create\n");
            fails++;
            return;
        }
    for (i = 0; i < THREADS; i++)
        pthread_join(th[i], 0);
    chk("hammer.add", counter, (long)THREADS * ITERS);
    chk("hammer.cas", cas_counter, (long)THREADS * ITERS);
}

int main(void)
{
    single_threaded();
    threaded();
    printf(fails ? "FAILURES %d\n" : "OK\n", fails);
    return fails != 0;
}
