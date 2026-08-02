// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
extern void consume_int(int *);
void unused_but_set_address(void)
{
    int x;
    x = 5;
    consume_int(&x);
}
