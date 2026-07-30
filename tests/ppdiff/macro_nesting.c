#define e(x) x
e(e(e(42)))
#define twice(m) m m
#define once foo
twice(once)
