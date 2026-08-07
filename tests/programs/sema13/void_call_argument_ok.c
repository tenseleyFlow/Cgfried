// FLAGS: -fsyntax-only
// The exemption half of void_call_argument.c. A void expression is legal
// wherever its VALUE is not used: as an expression statement, cast to void,
// as the operand of a comma, and returned from a void function. A rule that
// rejected those would break ordinary C.
void f(void);

void t(void)
{
    f();
    (void)f();
    (f(), f());
    return f();
}
