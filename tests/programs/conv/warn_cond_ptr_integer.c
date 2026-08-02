// FLAGS: -fsyntax-only
// WARNING_EXPECTED: pointer/integer type mismatch in conditional expression

int a;

int f(int c)
{
    a = c ? a : &a;
    return a;
}
