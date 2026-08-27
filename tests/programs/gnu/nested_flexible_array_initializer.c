// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: initialization of flexible array member in a nested context
struct Inner {
    int value;
    int tail[];
};

struct Outer {
    struct Inner inner;
};

static struct Outer object = {{1, {2}}};
