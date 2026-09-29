// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: may not be passed through an unprototyped or variadic argument
/* The admitted TI vector has a measured contract only for named parameters.
 * Keep anonymous arguments fail-closed until va_list transport is audited. */
typedef unsigned __int128 __attribute__((vector_size(16))) V;

void sink(int tag, ...);

void test(void)
{
    V value = (V){1};
    sink(0, value);
}
