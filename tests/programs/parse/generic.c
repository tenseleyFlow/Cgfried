// FLAGS: --dump-ast
// CHECK: EXPR (y = (_Generic x <int>:1 <char>:2 default:3))
// CHECK: EXPR (y = (_Generic x default:0 <int>:1))
// CHECK: EXPR (y = (_Generic x <int>:(_Generic y <int>:2)))
// The controlling expression is never evaluated (6.5.1.1p2) and `default`
// may appear anywhere in the list, not only last.
int x, y;
void f(void) {
    y = _Generic(x, int: 1, char: 2, default: 3);
    y = _Generic(x, default: 0, int: 1);
    y = _Generic(x, int: _Generic(y, int: 2));
}
