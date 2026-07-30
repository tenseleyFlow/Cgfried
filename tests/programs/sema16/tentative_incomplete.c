// FLAGS: -fsyntax-only
// ERROR_EXPECTED: storage size of 'st' isn't known
// 6.9.2p3: an INTERNAL-linkage tentative must have a complete type by end
// of TU — nothing outside this TU can ever complete it.
struct S;
static struct S st;
