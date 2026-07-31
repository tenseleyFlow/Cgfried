// F-S22-MIRCHECK regression pin: an unmatched MIR_CHECK must FAIL.
// The directive parser validated MIR_CHECK and then silently dropped it
// (missing from the add_dir list), so every Sprint 21 MIR golden passed
// vacuously.
// FLAGS: -emit-mir
// MIR_CHECK: zzz_never_printed
int f(void) { return 1; }
