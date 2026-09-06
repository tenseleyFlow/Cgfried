// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: initialization of a flexible array member
struct Bytes {
    int tag;
    unsigned char data[];
};

union Storage {
    struct Bytes bytes;
    unsigned char backing[16];
};

static union Storage object = {{1, {2}}};
