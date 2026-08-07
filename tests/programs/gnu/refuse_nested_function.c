// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: nested functions are not supported
// A REFUSED row of docs/gnu-extensions.md. gcc implements these with an
// executable trampoline on the stack.
//
// Before this diagnostic the declarator simply ran out and reported
// "expected ';' after declaration", which tells the reader nothing about
// why. gnu_block_scope_decl.c pins that an ordinary block-scope function
// DECLARATION still compiles -- the two differ only by a brace.
int f(void)
{
    int g(void) { return 1; }

    return g();
}
