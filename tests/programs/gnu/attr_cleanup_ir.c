// FLAGS: -emit-ir
/* Two orderings a running program cannot observe, pinned in the IR instead.
 *
 * BOTH are cases where the wrong order still produces a program that runs and
 * usually prints the right answer, so an execution fixture would pass on a
 * compiler that had them backwards. tests/corpus/x86_64/int/attr_cleanup.c
 * covers everything execution CAN see; this covers the rest.
 *
 * 1. The call comes before the stackrestore. A cleanup function receives a
 *    pointer to its variable, and in a scope that also holds a VLA that
 *    pointer is into storage the restore releases. Released stack is still
 *    readable, so getting this backwards reads plausible values almost every
 *    time and corrupts only under a signal or a reentrant call.
 *
 * 2. `return` loads the value BEFORE the cleanups run. gcc does the same
 *    (measured: a cleanup that writes 99 to its variable does not change what
 *    `return x` returns), and the corpus fixture asserts the visible half.
 *    This asserts the shape, so the two cannot drift.
 *
 * IR_CHECK matches IN ORDER, which is the whole point here -- these are
 * claims about sequence, not about presence. */
void t(int *p);

// IR_CHECK: func void @with_vla
// IR_CHECK: stacksave
// IR_CHECK: call void @t(
// IR_CHECK: stackrestore
void with_vla(int n)
{
    int a __attribute__((cleanup(t))) = 1;
    int v[n];

    (void)v;
    (void)a;
}

// IR_CHECK: func i32 @with_return
// IR_CHECK: load i32
// IR_CHECK: call void @t(
// IR_CHECK: ret i32
int with_return(void)
{
    int a __attribute__((cleanup(t))) = 1;

    return a;
}
