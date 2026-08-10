// FLAGS: -fsyntax-only -std=gnu17
// WARN_COUNT: 1
/* A registry SUPPLEMENT: gcc 8, gcc 16 and clang all emit this with NO flag
 * at all, so unlike prio-ctor-dtor there was no modern spelling to adopt and
 * the name is ours. That is the whole reason it needs a row -- a warning a
 * user cannot silence is what the registry exists to prevent.
 *
 * A reversed range matches nothing, so `f(2)` returns 0. The second switch
 * is the silent half: a one-value range is not empty, and a fixture with
 * only the firing case would pass on a compiler that warned for every
 * range. WARN_COUNT is what holds both halves. */
int f(int x)
{
    switch (x) {
    // WARN_CHECK: switch-empty-range empty range specified
    case 3 ... 1:
        return 9;
    default:
        return 0;
    }
}

int g(int x)
{
    switch (x) {
    case 5 ... 5:
        return 7;
    default:
        return 0;
    }
}
