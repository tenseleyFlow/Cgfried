// FLAGS: -S
// ASM_CHECK(x86_64-linux-gnu): {{movq[ \t]+%fs:0, %r}}
// ASM_CHECK(x86_64-linux-gnu): {{leaq[ \t]+tv@tpoff\(%r}}
// ASM_CHECK(x86_64-linux-gnu): {{\.section[ \t]+\.tdata,"awT",@progbits}}
// ASM_CHECK(x86_64-linux-gnu): {{\.type[ \t]+tv, @tls_object}}
// ASM_CHECK(x86_64-linux-gnu): {{\.section[ \t]+\.tbss,"awT",@nobits}}
// The checks are in emission order: functions precede data in our output,
// and ASM_CHECK matches in order.
//
// arm64 puts the thread pointer in an architectural register instead of a
// segment base, and splits the offset into two 12-bit halves. Its TLS data
// is emitted BEFORE the functions -- gas rejects a TLS relocation naming a
// symbol it has not yet seen defined in a TLS section -- so the section
// check comes first there.
// ASM_CHECK(arm64-linux): {{\.section[ \t]+\.tdata,"awT",@progbits}}
// ASM_CHECK(arm64-linux): {{mrs[ \t]+x[0-9]+, tpidr_el0}}
// ASM_CHECK(arm64-linux): {{add[ \t]+x[0-9]+, x[0-9]+, #:tprel_hi12:tv, lsl #12}}
// ASM_CHECK(arm64-linux): {{add[ \t]+x[0-9]+, x[0-9]+, #:tprel_lo12_nc:tv}}
// The local-exec model. A thread-local has no ordinary address: the thread
// pointer comes out of %fs:0 and the symbol's link-time offset is added to
// it, which is what R_X86_64_TPOFF32 resolves. Building the ADDRESS rather
// than folding %fs:sym@tpoff into every access keeps one code path for
// loads, stores and address-of alike.
//
// Before Sprint 51 all of this lowered to an ordinary global and every
// thread shared one copy -- a silent miscompile with no diagnostic. See
// .docs/audits/tls-debt.md.
_Thread_local int tv = 7;
_Thread_local long tz;

int get(void) { return tv; }
void set(int v) { tv = v; }
long *addr(void) { return &tz; }
