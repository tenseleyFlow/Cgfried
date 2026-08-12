// FLAGS: -std=gnu17
// OPT_EQ: all
// EXIT_CODE: 0
/* Every `m` operand needs a register for its address at the asm site, but the
 * six addresses must not become unspillable for the whole interval spanning
 * the clobbering asm.  There are only five callee-saved allocatable GP
 * registers, so whole-interval pinning makes this valid program exhaust the
 * allocator at -O2 (found while compiling musl's pthread_barrier_wait.c). */
static int touch_six(int *a, int *b, int *c, int *d, int *e, int *f)
{
    __asm__ volatile("" : : "m"(*a), "m"(*b), "m"(*c), "m"(*d), "m"(*e),
                     "m"(*f));
    __asm__ volatile("" : : : "rax");
    __asm__ volatile("" : : "m"(*a), "m"(*b), "m"(*c), "m"(*d), "m"(*e),
                     "m"(*f));
    return *a + *b + *c + *d + *e + *f;
}

int main(void)
{
    int v[6] = {1, 2, 3, 4, 5, 6};

    return touch_six(v, v + 1, v + 2, v + 3, v + 4, v + 5) != 21;
}
