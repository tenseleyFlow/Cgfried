// FLAGS: -fsyntax-only -std=gnu17 -Wall
// WARN_COUNT: 7
/* `deprecated`, in every position gcc accepts, with the message form and
 * the bare form. Byte-identical to gcc 16 on all seven lines.
 *
 * THE SILENT HALF IS THE POINT. The warning fires at the USE and never at
 * the declaration, and the DEFINITION of a deprecated function is silent --
 * so `f`'s three declarations and its definition contribute nothing, while
 * its two uses contribute two. A fixture that only counted the firing
 * lines would pass on a compiler that warned at every declaration too.
 * `n` and `EW` are the undeprecated siblings: they catch an implementation
 * that lets the attribute leak to the rest of a record or enumerator list,
 * which an early draft of this one did.
 *
 * An ENUMERATOR takes the attribute AFTER its name. gcc rejects
 * `__attribute__((X)) EV` outright with "expected identifier", so a probe
 * written that way measures nothing -- which is how the first version of
 * this feature came to claim gcc does not support deprecated enumerators
 * in C at all. It does. */
__attribute__((deprecated)) int f(void);
__attribute__((deprecated("use g2"))) int g(void);
__attribute__((deprecated)) int obj;
typedef __attribute__((deprecated)) int dep_int;
struct S {
    int m __attribute__((deprecated));
    int n;
};
enum E { EV __attribute__((deprecated)) = 1, EW = 2 };

int f(void)
{
    return 1;
}

int use(struct S *s)
{
    // WARN_CHECK: deprecated-declarations 'f' is deprecated
    int a = f();
    // WARN_CHECK: deprecated-declarations 'g' is deprecated: use g2
    int b = g();
    int c = obj;
    dep_int d = 0;
    // WARN_CHECK: deprecated-declarations 'm' is deprecated
    int e = s->m + s->n;
    // WARN_CHECK: deprecated-declarations 'EV' is deprecated
    int h = EV + EW;
    int (*fp)(void) = f;

    return a + b + c + d + e + h + (fp != 0);
}
