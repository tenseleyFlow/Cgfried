// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: arrays of 'vector_size' type are not yet supported
// ERROR_EXPECTED: record members with 'vector_size' type are not yet supported
// ERROR_EXPECTED: volatile 'vector_size' types are not yet supported
// ERROR_EXPECTED: static-storage 'vector_size' objects are not yet supported
// ERROR_EXPECTED: casts involving volatile or atomic 'vector_size' types
// ERROR_EXPECTED: casts involving arrays of 'vector_size' type are not yet supported
// ERROR_EXPECTED: '__builtin_va_arg' of a 'vector_size' type is not yet supported
// ERROR_EXPECTED: arrays of 'vector_size' compound literals are not yet supported
// ERROR_EXPECTED: volatile or atomic 'vector_size' compound literals
// ERROR_EXPECTED: static-storage 'vector_size' compound literals
/* Keep the one-lane call boundary from silently becoming general vector
 * object support through derived types or unsupported access modes. */
typedef unsigned __int128 __attribute__((vector_size(16))) V;

V array[2];
struct S {
    V member;
};
volatile V volatile_value;
static V static_value;
V *static_literal = &(V){2};

void derived_uses(__builtin_va_list ap, void *raw)
{
    (void)*(volatile V *)raw;
    (void)*(V(*)[2])raw;
    (void)__builtin_va_arg(ap, V);
    (void)(V[2]){{4}, {5}};
    (void)(volatile V){3};
}
