#define V(a, ...) a : __VA_ARGS__
V(1, 2, 3)
V(1)
#define I(x) x
I() end
#define f2(a,b) a b
f2((x,y),z)
