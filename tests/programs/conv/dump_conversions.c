// FLAGS: -fdump-sema
// CHECK: EXPR (i = ((icast<int> us1) - (icast<int> us2))) : int
// CHECK: EXPR (l = (l + (icast<long> ui))) : long
// CHECK: EXPR (i = (1 << 40L)) : int
// CHECK: EXPR (i = (icast<int> c)) : int
// CHECK: EXPR (p = (icast<int *> a)) : int *
// CHECK: EXPR (d = (icast<double> i)) : double
// Every implicit conversion is MATERIALIZED as an `icast` node. Later
// passes read the tree; a conversion that exists only as a rule is one
// that gets applied twice or not at all.
//
// The three that matter most here: `us1 - us2` promotes BOTH to signed
// int (so the subtraction can go negative); `l + ui` converts the
// unsigned int to long rather than both to unsigned, because a 64-bit
// long represents every unsigned int; and `1 << 40L` keeps type int,
// because a shift takes the promoted LEFT operand alone.
void f(void) {
    unsigned short us1, us2;
    long l; unsigned int ui;
    int i; char c; double d;
    int a[4]; int *p;
    i = us1 - us2;
    l = l + ui;
    i = 1 << 40L;
    i = c;
    p = a;
    d = i;
}
