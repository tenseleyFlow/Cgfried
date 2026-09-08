// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: cannot take the address of a scalar field with reverse
// storage order
#define BE __attribute__((scalar_storage_order("big-endian")))

struct S {
    unsigned int value;
} BE;

unsigned int *address(struct S *s)
{
    return &s->value;
}
