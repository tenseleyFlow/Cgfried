// RESOLVED(audit): OPT-H-04 correlated pointer selects collapse to a false must-alias proof
// The two selected pointers always name opposite array elements.  Their
// individual offset hulls are equal, but no concrete execution makes them
// equal; forwarding the second store through a must-alias answer is wrong.
typedef int (*ProbeFn)(int);

int probe(int choose_first)
{
    int a[2] = {0, 0};
    int *p = choose_first ? &a[0] : &a[1];
    int *q = choose_first ? &a[1] : &a[0];

    *p = 11;
    *q = 22;
    return *p;
}

int main(int argc, char **argv)
{
    ProbeFn fn = probe;

    (void)argv;
    return fn(argc > 1) != 11;
}
