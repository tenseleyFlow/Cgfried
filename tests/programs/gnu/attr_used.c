// FLAGS: -S -O2
// ASM_CHECK(x86_64-linux-gnu): {{^u_fn:}}
// ASM_CHECK-NOT(x86_64-linux-gnu): {{^plain_fn:}}
// `used` keeps a symbol nothing in this translation unit references.
//
// The NEGATIVE is the whole fixture. A positive check alone passes whether or
// not anything is ever dropped, so it would go green on a compiler that
// removes nothing — the vacuous pass this project keeps re-finding.
// `ASM_CHECK-NOT` was added for exactly this: an absence is not expressible
// with a positive check, and "the symbol was DROPPED" is the claim under test.
//
// -O2 is required: at -O0 nothing is removed and BOTH survive, so the pair
// only means something once IPO is actually deleting unreachable functions.
//
// `used` and an `alias` target reach the same root set from different
// directions — one is said in the source, the other is implied by a `.set`
// the callgraph cannot see. They share the code for that reason.
static int u_fn(void) __attribute__((used));
static int u_fn(void)
{
    return 1;
}

/* No attribute, no reference: IPO deletes it, and gcc agrees. */
static int plain_fn(void)
{
    return 2;
}

int keep(void)
{
    return 0;
}
