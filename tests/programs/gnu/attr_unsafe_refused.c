// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: the 'packed' attribute is not yet implemented
// The other half of the classification. `packed` changes LAYOUT, so
// accepting and ignoring it would give a struct the wrong offsets and a
// program that links and then misbehaves -- the failure mode
// docs/gnu-extensions.md exists to prevent.
//
// This fixture is expected to flip from error to behaviour when packed
// lands; the tier table and check_gnu_tiers.sh are what keep the two in
// step. Until then a hard error is the honest answer, and it is what makes
// implementing the unsafe set incrementally safe: at every point the
// compiler either does the right thing or refuses.
struct S {
    char a;
    int b;
} __attribute__((packed));

int main(void)
{
    return (int)sizeof(struct S) - 5;
}
