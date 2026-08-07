// FLAGS: -fsyntax-only -std=gnu17
// WARNING_EXPECTED: 'packed' attribute ignored
// WARN_COUNT: 3
// `packed` that names neither a record definition nor a member has nothing to
// pack. gcc warns rather than erroring, and so do we -- but it must be SAID.
// A silently dropped layout attribute is the failure mode
// docs/gnu-extensions.md exists to prevent, and this is the one position where
// dropping it is correct. WARN_COUNT pins all three sites; WARN_CHECK anchors
// to the line below it, so it can only speak for the first.
// WARN_CHECK: attributes 'packed' attribute ignored
int w __attribute__((packed));

struct S {
    char a;
    int b;
};

/* On an OBJECT of packed-able type -- still nothing to pack: the attribute
 * would have had to name the record definition. */
struct S v __attribute__((packed));
static char c __attribute__((packed));

int main(void)
{
    return (int)(sizeof(v) + w + c);
}
