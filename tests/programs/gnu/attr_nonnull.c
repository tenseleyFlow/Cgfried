// FLAGS: -fsyntax-only -std=gnu17 -Wall
// WARN_COUNT: 6
/* `nonnull(1,2,...)` and the bare form. Byte-identical to gcc 16.
 *
 * THE BARE FORM IS NOT "no positions", it is EVERY POINTER PARAMETER --
 * `fall` proves it. A zero mask would otherwise be indistinguishable from
 * "all", so the two forms carry separate state.
 *
 * `(int *)0` is NOT a null pointer constant by 6.3.2.3p3 -- only a zero ICE
 * or one cast to `void *` qualifies -- and gcc warns for it anyway. So this
 * check strips an explicit pointer cast before the test, while conv_is_npc
 * stays strict for its other caller, which decides the TYPE of a
 * conditional expression and needs the standard's narrow rule.
 *
 * Two silent cases carry the fixture: a null in an UNLISTED position
 * (`f1(ok, 0)`) and a function with no attribute at all. Without them this
 * would pass on a compiler that warned for every null argument. */
__attribute__((nonnull(1))) void f1(int *p, int *q);
__attribute__((nonnull(1, 2))) void f2(int *p, int *q);
__attribute__((nonnull)) void fall(int *p, int *q);
void plain(int *p);

void use(int *ok)
{
    // WARN_CHECK: nonnull argument 1 null where non-null expected
    f1(0, 0);
    f1(ok, 0);
    f2(ok, 0);
    f2(0, ok);
    fall(0, ok);
    plain(0);
    f1((void *)0, ok);
    f1((int *)0, ok);
}
