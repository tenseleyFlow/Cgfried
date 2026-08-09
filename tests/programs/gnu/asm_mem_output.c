// FLAGS: -fsyntax-only -std=gnu17
/* A MEMORY output takes no register, so it does not count against the
 * one-register-output limit: this is musl's atomic shape (`"=r"(v),
 * "=m"(*p)`) and it must compile. */
int f(int *p)
{
    int v;

    __asm__("movl $7, %0\n\tmovl %0, %1" : "=r"(v), "=m"(*p));
    return v;
}
