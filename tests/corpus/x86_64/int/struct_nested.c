// Nested aggregates: member paths through two levels + array of struct.
// EXIT_CODE: 0
struct In {
    int a, b;
};
struct Out {
    struct In in;
    int tail[2];
};
int main(void)
{
    struct Out o[2];
    o[0].in.a = 1;
    o[0].in.b = 2;
    o[0].tail[0] = 3;
    o[0].tail[1] = 4;
    o[1] = o[0];
    o[1].in.b = 20;
    if (o[0].in.b != 2)
        return 1;
    if (o[1].in.a + o[1].in.b + o[1].tail[0] + o[1].tail[1] != 28)
        return 2;
    return 0;
}
