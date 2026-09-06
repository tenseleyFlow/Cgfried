// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: initialization of flexible array member in a nested context
struct Bytes {
    int tag;
    unsigned char data[];
};

union Tiny {
    struct Bytes bytes;
    int backing;
};

static union Tiny object = {{1, {2}}};
