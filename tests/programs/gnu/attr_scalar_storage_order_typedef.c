// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: 'scalar_storage_order' attribute on a typedef is not yet
// supported
#define BE __attribute__((scalar_storage_order("big-endian")))

struct S {
    unsigned int value;
};

typedef struct S AttributedS BE;
