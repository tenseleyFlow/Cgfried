// FLAGS: -fsyntax-only -Wpointer-arith
// WARN_COUNT: 1
void *pointer_arith_void(void *p)
{
    // WARN_CHECK: pointer-arith pointer of type 'void *' used in arithmetic
    return p + 1;
}
