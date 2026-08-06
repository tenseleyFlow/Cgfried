// FLAGS: -c
// ERROR_EXPECTED: thread-local storage is not lowered yet: lands in Sprint 50
// _Thread_local parsed, typed and recorded correctly, and then lowered to an
// ORDINARY GLOBAL: every thread shared one copy. Four threads each
// incrementing this a thousand times left 1000 in main where gcc leaves 0,
// with no diagnostic anywhere. Refusing to emit beats emitting a plausible
// wrong answer -- the Sprint 28 rule about stubs that return plausible
// values. The sema-level rules are unaffected and still tested by the
// tls_*.c fixtures beside this one, which is why the refusal lives in
// lowering rather than in sema. See .docs/audits/tls-debt.md.
_Thread_local int counter;

int bump(void)
{
    return ++counter;
}
