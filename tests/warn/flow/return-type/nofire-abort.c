// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void abort(void);
int flow_ends_abort(void)
{
    abort();
}
