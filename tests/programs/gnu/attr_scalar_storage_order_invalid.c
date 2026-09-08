// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: unknown scalar storage order 'middle-endian'
struct S {
    unsigned int value;
} __attribute__((scalar_storage_order("middle-endian")));
