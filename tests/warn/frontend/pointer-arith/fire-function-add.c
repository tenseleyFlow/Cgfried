// FLAGS: -fsyntax-only -Wpointer-arith
// WARN_COUNT: 1
static int pointed_function(void) { return 0; }
void pointer_arith_function(void)
{
    // WARN_CHECK: pointer-arith used in arithmetic
    pointed_function + 1;
}
