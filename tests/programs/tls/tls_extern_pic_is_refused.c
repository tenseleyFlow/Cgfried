// FLAGS: -fPIC -S
// SKIP(arm64-macos): ELF general-dynamic TLS is not a Mach-O ABI model
// ERROR_EXPECTED: general-dynamic TLS model is not supported yet
// Full PIC can name a thread-local object in an arbitrary shared object. Its
// ABI model is general-dynamic, not the initial-exec sequence valid for the
// executable-only paths covered by TLS-005's first half. Keep this a clear
// error until the dynamic resolver path exists; emitting a plausible
// initial-exec access here would be a silent ABI error.
extern _Thread_local int elsewhere;

int read_it(void) { return elsewhere; }
