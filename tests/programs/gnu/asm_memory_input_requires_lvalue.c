// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: an asm memory operand with constraint "m" must be an lvalue
// Frontend fuzzer seed 12443 reached lower_lvalue with AST_EXPR_SIZEOF here.
// A memory constraint needs the address of an object; sizeof produces a value.
void f(int *p)
{
    __asm__ volatile("" : : "m"(sizeof *p));
}
