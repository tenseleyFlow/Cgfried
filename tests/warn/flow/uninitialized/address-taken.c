// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void flow_define_through_pointer(int *);
int flow_address_taken(void)
{
    int x;
    flow_define_through_pointer(&x);
    return x;
}
