// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: 'asm goto' is not supported
// A REFUSED row of docs/gnu-extensions.md. Refusing beats parsing-and-
// ignoring: jumping out of an asm block creates control-flow edges the IR
// verifier could only trust rather than check, so accepting one would put
// unverifiable edges into every later pass.
//
// The qualifiers are deliberate -- `goto` may sit behind any number of
// volatile/inline spellings, and looking only at the token right after
// `asm` would miss it and fall through to the ordinary asm message.
void f(void)
{
    __asm__ __volatile__ goto("" : : : : lab);
lab:;
}
