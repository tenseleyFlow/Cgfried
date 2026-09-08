// FLAGS: -fsyntax-only -std=gnu17
// WARNING_EXPECTED: address of array with reverse scalar storage order
// requested
#define BE __attribute__((scalar_storage_order("big-endian")))

struct S {
    unsigned int values[2];
} BE;

unsigned int *decay(struct S *s)
{
    return s->values;
}
