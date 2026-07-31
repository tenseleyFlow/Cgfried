// Bitfield increment loop: 4-bit field wraps at 16.
// EXIT_CODE: 2
struct B {
    unsigned n : 4;
};
int main(void)
{
    struct B b = {0};
    int k;
    for (k = 0; k < 18; k++)
        b.n++;
    return b.n;
}
