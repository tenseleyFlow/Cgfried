// FLAGS: -fsyntax-only -std=gnu17 -Wall -Wextra -O1
// WARN_COUNT: 1
/* `noreturn`. The only one of D3's five that feeds ANALYSIS rather than
 * emitting a diagnostic of its own, so its value is measured in FALSE
 * POSITIVES REMOVED -- which is why this fixture is mostly silent.
 *
 * Without it, `f` draws "control reaches end of non-void function" (it ends
 * in a call that never returns) and `h` draws "may be used uninitialized"
 * (the else branch dies, so `v` is set on every path that reaches the
 * return). gcc is silent for both. The ONE warning that must remain is
 * `g`'s: `plain_die` carries no attribute, so control really can reach the
 * end there -- and that is the half that catches an implementation which
 * treats every call as noreturn.
 *
 * The attribute joins C11 `_Noreturn` and the hardcoded library-name list
 * at one decision in lower_call rather than becoming a second mechanism. */
__attribute__((noreturn)) void die(void);
void plain_die(void);

int f(int x)
{
    if (x)
        return 1;
    die();
}

// WARN_CHECK: return-type control reaches end of non-void function
int g(int x)
{
    if (x)
        return 1;
    plain_die();
}

int h(int x)
{
    int v;

    if (x)
        v = 1;
    else
        die();
    return v;
}
