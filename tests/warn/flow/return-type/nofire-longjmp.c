// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void longjmp(void *, int);
int flow_ends_longjmp(void *buffer)
{
    longjmp(buffer, 1);
}
