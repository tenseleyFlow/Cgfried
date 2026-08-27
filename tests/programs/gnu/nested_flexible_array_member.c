// FLAGS: -std=gnu17 -fsyntax-only
struct S;

struct C {
    int value;
    struct S *table[];
};

struct S {
    struct C child;
};

void use(struct S *object)
{
    use(((void)1, object->child).table[0]);
    ((void)1, object->child).table[0] = 0;
}
