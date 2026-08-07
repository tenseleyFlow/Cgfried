// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: the 'section' attribute is not yet implemented
// The other half of the classification. `section` decides which output
// section an object lands in, so accepting and ignoring it would put the
// object somewhere the author did not ask for -- a program that links and
// then misbehaves, the failure mode docs/gnu-extensions.md exists to prevent.
//
// This fixture named `packed` until packed landed, and it is expected to be
// retargeted again as each unsafe row is implemented. The tier table and
// check_gnu_tiers.sh are what keep code and document in step. Until then a
// hard error is the honest answer, and it is what makes implementing the
// unsafe set incrementally safe: at every point the compiler either does the
// right thing or refuses.
int boot_data __attribute__((section(".boot"))) = 1;

int main(void)
{
    return boot_data - 1;
}
