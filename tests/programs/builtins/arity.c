// Arity comes from the table in src/builtins.def, so a wrong count is a
// clean diagnostic naming the builtin rather than a crash downstream.
// ERROR_EXPECTED: '__builtin_trap' takes exactly 0 arguments
void f(void)
{
    __builtin_trap(1);
}
