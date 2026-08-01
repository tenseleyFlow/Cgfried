// OPT_EQ: all
// Deep recursion: ackermann(2,3) = 9 — frame correctness under real
// call depth.
// EXIT_CODE: 9
static int ack(int m, int n)
{
    if (m == 0)
        return n + 1;
    if (n == 0)
        return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}
int main(void)
{
    return ack(2, 3);
}
