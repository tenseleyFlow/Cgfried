// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use error-code returns or move it to a non-safe TU
void _longjmp(void *, int);
void __longjmp(void *, int);
void __longjmp_chk(void *, int);

void (*restore_one)(void *, int) = _longjmp;
void (*restore_two)(void *, int) = __longjmp;
void (*restore_three)(void *, int) = __longjmp_chk;
