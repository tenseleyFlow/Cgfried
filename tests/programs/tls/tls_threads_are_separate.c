// ENV: CGF_AS=0
// FLAGS: -lpthread
// CHECK: main counter=0
// EXIT_CODE: 0
// THE test that matters: spelling the sections right proves nothing about
// semantics. Four threads each increment a thread-local a thousand times;
// main must still see ZERO, because main's copy is its own. Before Sprint
// 51 this printed 1000 -- one shared copy -- and no test caught it because
// no test ran two threads.
//
// CGF_AS=0 because the bundled assembler has neither the %fs: segment
// override nor @tpoff (TLS-004); the driver says so cleanly rather than
// letting the assembler reject correct assembly.
#include <pthread.h>
#include <stdio.h>

_Thread_local int counter;

static void *worker(void *p)
{
    int i;

    (void)p;
    for (i = 0; i < 1000; i++)
        counter++;
    return 0;
}

int main(void)
{
    pthread_t t[4];
    int i;

    for (i = 0; i < 4; i++)
        pthread_create(&t[i], 0, worker, 0);
    for (i = 0; i < 4; i++)
        pthread_join(t[i], 0);
    printf("main counter=%d\n", counter);
    return 0;
}
