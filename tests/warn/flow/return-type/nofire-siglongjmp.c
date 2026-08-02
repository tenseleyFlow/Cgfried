// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void siglongjmp(void *, int);
int flow_ends_siglongjmp(void *buffer)
{
    siglongjmp(buffer, 1);
}
