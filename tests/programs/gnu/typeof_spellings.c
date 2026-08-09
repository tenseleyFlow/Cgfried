// FLAGS: -fsyntax-only -std=gnu17
// WARN_COUNT: 0
/* All three GNU spellings name the same construct. gcc takes `typeof`,
 * `__typeof__` and `__typeof` alike, and all three were already keywords in
 * our table -- unlike `__volatile`, whose absence from the asm qualifier
 * loops gated every musl TU that includes atomics. Checking the whole family
 * rather than the one spelling in front of me is the lesson from that. */
int spellings(void)
{
    int a = 1;
    typeof(a) x = a;
    __typeof__(a) y = a;
    __typeof(a) z = a;

    return x + y + z;
}
