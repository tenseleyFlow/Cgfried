// OPT_EQ: all
// Sparse cases: the compare tree, executed.
// EXIT_CODE: 30
static int pick(int n)
{
    switch (n) {
    case 10:
        return 1;
    case 100:
        return 2;
    case 1000:
        return 4;
    case 10000:
        return 8;
    default:
        return 16;
    }
}
int main(void)
{
    return pick(10) + pick(1000) + pick(10000) + pick(7) + pick(10);
}
