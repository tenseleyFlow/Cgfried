// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int setjmp(void *);
int flow_setjmp_caller(void *buffer)
{
    int x;
    if (setjmp(buffer))
        x = 1;
    return x;
}
