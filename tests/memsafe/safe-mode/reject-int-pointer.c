// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: integer-to-pointer casts; use the documented uintptr_t
// round-trip whitelist
void *reject_int_pointer(unsigned long value)
{
    return (void *)value;
}
