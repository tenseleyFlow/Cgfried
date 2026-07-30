typedef int T;
struct S { int m; int arr[2]; };
int g(int);
int f(int a, int b, struct S *sp, int *p) {
    int x = 0;
    x = a + b * 2 - a / b % 3;
    x = a << b >> 1;
    x = a < b == b > a;
    x = a & b | b ^ a;
    x = a && b || !a;
    x = a ? b : a ? x : b;
    x = (x = a) , (x = b);
    x += a; x -= b; x *= a; x /= b; x %= a;
    x <<= 1; x >>= 1; x &= a; x |= b; x ^= a;
    x = ~a; x = -a; x = +a; x = !a;
    x = ++a; x = a++; x = --b; x = b--;
    x = *p; p = &x;
    x = sp->m; x = (*sp).m; x = sp->arr[1];
    x = g(a); x = g(g(a)); x = g(a) + g(b);
    x = (T)a; x = (char)a; x = (T)(char)a;
    x = sizeof(T); x = sizeof x; x = sizeof(struct S);
    x = _Alignof(T);
    x = _Generic(a, int: 1, default: 0);
    x = (T){5};
    x = (int[]){1, 2, 3}[1];
    x = g(a + b);
    return x;
}
