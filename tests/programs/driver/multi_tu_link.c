// THE multi-TU milestone shape (DoD 3): two TUs, one link, one run.
// The aux TU is compiled by the same driver invocation and linked in
// argv order.
// FLAGS: tests/fixtures/driver/aux_two.c
// EXIT_CODE: 42
int aux_two_add(int a, int b);
int main(void)
{
    return aux_two_add(40, 2);
}
