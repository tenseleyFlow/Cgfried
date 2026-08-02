// FLAGS: -fsyntax-only -Wpointer-arith
// WARN_COUNT: 1
unsigned long pointer_arith_sizeof(void)
{
    // WARN_CHECK: pointer-arith invalid application of 'sizeof' to type 'void'
    return sizeof(void);
}
