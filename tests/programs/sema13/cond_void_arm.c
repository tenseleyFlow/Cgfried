// EXIT_CODE: 0
// A conditional with ONE void arm. 6.5.15p3 allows void only when BOTH arms
// are void, but gcc and clang accept one and give the whole expression void
// type -- so rejecting it would refuse code that builds everywhere else.
//
// Handling only the both-void case let one-sided void fall through to the
// pointer/integer arm, which treated `void` as the integer side and gave the
// CONDITIONAL a pointer type while one arm produced no value at all. Lowering
// then asked for the IR type of void: "lower_irtype on non-scalar type kind
// 0". Frontend fuzzer, seed 76632, found by the 100k sanitized CI run rather
// than the 2000-iteration smoke -- the reason that job is separate.
//
// The arithmetic case had the milder version of the same bug: a hard error
// where gcc is silent.
static int hits;

void a(void)
{
    hits += 1;
}
void b(void)
{
    hits += 10;
}
int g;

void ptr_and_void(void)
{
    g ? a : b(); /* function designator vs void: the ICE */
}

void int_and_void(void)
{
    g ? 1 : b(); /* was a spurious hard error */
}

void both_void(void)
{
    g ? a() : b(); /* the case that always worked */
}

int main(void)
{
    /* The arms must still EXECUTE: giving the conditional void type must not
     * quietly discard the side effects of the branch that runs. */
    g = 0;
    ptr_and_void(); /* g false -> b() */
    int_and_void(); /* g false -> b() */
    both_void();    /* g false -> b() */
    if (hits != 30)
        return 1;
    g = 1;
    both_void(); /* g true -> a() */
    if (hits != 31)
        return 2;
    return 0;
}
