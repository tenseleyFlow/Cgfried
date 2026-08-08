// ERROR_EXPECTED: incompatible types
// The other half of cond_void_arm.c: the expression is ACCEPTED, but its value
// is void and using one is still an error -- gcc's "void value not ignored as
// it ought to be". Accepting the expression must not mean accepting its value,
// or the fix would have traded an ICE for a silent wrong type.
//
// It also pins the message: `what` was the literal string "error" while the
// diagnostic printer already prefixes the level, so every one of these read
// "error: error:". Nothing had pinned it because the path was barely
// reachable until the conditional above started producing void.
void a(void);
void b(void);
int g;

void f(void)
{
    void *p = g ? a : b();

    (void)p;
}

int main(void)
{
    return 0;
}
