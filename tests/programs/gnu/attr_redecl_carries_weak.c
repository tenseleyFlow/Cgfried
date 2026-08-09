// FLAGS: -S
// ASM_CHECK: .weak{{[ \t]}}wf
// The consequential half of attr_redecl_carries.c, kept separate because the
// claim is different in kind.
//
// An IR marker proves the fact reached the IR. What a linker acts on is the
// SYMBOL BINDING, and before the fix this emitted `.globl wf` -- verified with
// `readelf -sW`, which reported GLOBAL where gcc reports WEAK. A weak
// definition that binds strongly does not fail at compile time or at link
// time: it silently wins an overload it was written to lose, which is how
// musl's weak_alias idiom is supposed to work and the reason this shape
// matters more than the others.
//
// Checked here as the emitted directive so the fixture needs no external
// tools; the readelf comparison against gcc was done when the fix landed and
// is recorded in AGENTS.md rather than re-run per build.
void wf(void);
__attribute__((weak)) void wf(void)
{
}

int main(void)
{
    wf();
    return 0;
}
