// FLAGS: -fsyntax-only -std=gnu17
// WARNING_EXPECTED: 'scalar_storage_order' attribute ignored
#define BE __attribute__((scalar_storage_order("big-endian")))

struct S {
    unsigned int value;
};

struct S object BE;
