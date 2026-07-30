// FLAGS: --dump-ast
// CHECK: FUNCDEF add: func(K&R:a,b) ret int
// CHECK: FUNCDEF old: func(K&R:s) ret int
// CHECK: FUNCDEF proto: func(int, ptr to char) ret int
// A K&R definition's parameter TYPES live in the declaration list between
// the ')' and the '{', and a parameter with no declaration there is
// implicitly int. The identifier list is legal ONLY in a definition —
// see reject/kr_prototype.c for the other half of that rule.
int add(a, b)
int a;
int b;
{
    return a + b;
}

int old(s)
char *s;
{
    return 0;
}

int proto(int n, char *s)
{
    return n;
}
