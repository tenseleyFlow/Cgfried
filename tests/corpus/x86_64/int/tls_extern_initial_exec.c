// ENV: CGF_AS=0
// FLAGS: tests/fixtures/tls/tls_extern_initial_exec_def.c -lpthread
// OPT_EQ: all
// CHECK: main=5 worker=17
// EXIT_CODE: 0
// SKIP(arm64-macos): external ELF TLS initial-exec is not a Mach-O ABI model
// ASM_CHECK(x86_64-linux-gnu): {{movq[ \t]+elsewhere@GOTTPOFF\(%rip\), %r}}
// ASM_CHECK(x86_64-linux-gnu): {{movq[ \t]+%fs:0, %r}}
// ASM_CHECK(x86_64-linux-gnu): {{addq[ \t]+%r[a-z0-9]+, %r[a-z0-9]+}}
// ASM_CHECK-NOT(x86_64-linux-gnu): elsewhere@GOTPCREL
// ASM_CHECK-NOT(x86_64-linux-gnu): elsewhere@tpoff
// ASM_CHECK(arm64-linux): {{adrp[ \t]+x[0-9]+, :gottprel:elsewhere}}
// ASM_CHECK(arm64-linux): {{ldr[ \t]+x[0-9]+, \[x[0-9]+, :gottprel_lo12:elsewhere\]}}
// ASM_CHECK(arm64-linux): {{mrs[ \t]+x(12|13), tpidr_el0}}
// ASM_CHECK(arm64-linux): {{add[ \t]+x[0-9]+, x(12|13), x[0-9]+}}
// ASM_CHECK-NOT(arm64-linux): :got:elsewhere
// ASM_CHECK-NOT(arm64-linux): #:tprel_hi12:elsewhere
/* The definition is deliberately in a different translation unit. An extern
 * TLS object has no IrGlobal in this module, so this proves the fact survives
 * lowering as initial-exec metadata, reaches both emitters, links, and still
 * names a separate object for each thread. */
/* Keep the fixture header-free so cross-assembly coverage does not depend on
 * the host having the target's libc headers installed. pthread_t is an
 * unsigned-long handle on both supported Linux ABIs; the calls only pass it
 * through to libpthread. */
typedef unsigned long pthread_t;
extern int pthread_create(pthread_t *, const void *, void *(*)(void *),
                          void *);
extern int pthread_join(pthread_t, void **);
extern int printf(const char *, ...);

extern _Thread_local int elsewhere;

static void *worker(void *arg)
{
    int *observed = (int *)arg;

    elsewhere = 17;
    *observed = elsewhere;
    return 0;
}

int main(void)
{
    pthread_t thread;
    int worker_value = 0;

    elsewhere = 5;
    if (pthread_create(&thread, 0, worker, &worker_value) != 0)
        return 1;
    if (pthread_join(thread, 0) != 0)
        return 2;
    printf("main=%d worker=%d\n", elsewhere, worker_value);
    return elsewhere != 5 || worker_value != 17;
}
