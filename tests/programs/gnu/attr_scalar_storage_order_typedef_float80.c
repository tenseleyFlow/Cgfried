// FLAGS: --target=x86_64-linux-gnu -fsyntax-only -std=gnu17
// ERROR_EXPECTED: reverse scalar storage order for floating member 'value' is
// not yet supported
#define BE __attribute__((scalar_storage_order("big-endian")))

struct S {
    long double value;
};

typedef struct S SBE BE;
