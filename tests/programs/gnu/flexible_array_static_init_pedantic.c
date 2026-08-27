// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: initialization of a flexible array member
struct S {
    int head;
    int tail[];
};

static struct S object = {1, {2}};
