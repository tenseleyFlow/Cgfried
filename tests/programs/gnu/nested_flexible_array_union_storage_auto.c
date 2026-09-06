// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: initialization of flexible array member in a nested context
struct Bytes {
    int tag;
    unsigned char data[];
};

union Storage {
    struct Bytes bytes;
    unsigned char backing[16];
};

void f(void)
{
    union Storage automatic = {{1, {2}}};

    (void)automatic;
}
