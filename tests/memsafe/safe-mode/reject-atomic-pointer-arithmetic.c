// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: rejects atomic pointer arithmetic
void reject(_Atomic(int *) *pointer, unsigned long offset)
{
    *pointer += offset;
}
