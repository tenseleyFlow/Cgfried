// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: integer-to-pointer casts; use the documented uintptr_t
// round-trip whitelist
int *reject_direct(void)
{
    return (int *)4096UL;
}
