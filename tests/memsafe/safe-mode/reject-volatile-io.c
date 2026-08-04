// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: move the I/O boundary to a non-safe TU
int *reject_volatile_io(volatile unsigned long *port)
{
    return (int *)*port;
}
