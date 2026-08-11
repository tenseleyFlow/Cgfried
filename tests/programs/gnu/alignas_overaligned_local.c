// OPT_EQ: all
// Static and variably-modified automatic objects must honor the declaration's
// complete alignment, not merely the ABI's 16-byte stack alignment.
static int dynamic_local(int n)
{
    _Alignas(64) int values[n];

    values[0] = n;
    values[n - 1] = n + 1;
    return (((unsigned long)values & 63u) == 0) && values[0] == n &&
           values[n - 1] == n + 1;
}

int main(void)
{
    _Alignas(64) int loc = 2;

    if (((unsigned long)&loc & 63u) != 0 || loc != 2)
        return 1;
    return dynamic_local(7) ? 0 : 2;
}
