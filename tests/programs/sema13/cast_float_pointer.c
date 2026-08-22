// FLAGS: -fsyntax-only -std=c17 -pedantic-errors
// ERROR_EXPECTED: cannot cast between floating and pointer types
// Frontend fuzzer seed 84882 formerly emitted `fptoui f64 ... to ptr`, which
// is not a valid IR conversion. C provides no floating-to-pointer conversion.
void *f(double d)
{
    return (void *)d;
}
