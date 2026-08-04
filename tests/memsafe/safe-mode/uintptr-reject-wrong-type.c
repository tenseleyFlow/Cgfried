// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: integer-to-pointer casts; use the documented uintptr_t
// round-trip whitelist
int *reject_wrong_type(int *p)
{
    return (int *)(unsigned long)p;
}
