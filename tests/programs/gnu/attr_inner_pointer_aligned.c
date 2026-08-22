int *__attribute__((aligned(16))) *p;

typedef __typeof__(*p) aligned_pointer;

struct Holder {
    aligned_pointer value;
    char tail;
};

_Static_assert(_Alignof(__typeof__(p)) == 8,
               "the outer pointer keeps its natural alignment");
_Static_assert(_Alignof(aligned_pointer) == 16,
               "the inner pointer carries the requested alignment");
_Static_assert(sizeof(aligned_pointer) == 8,
               "a type alignment attribute does not change pointer size");
_Static_assert(_Alignof(struct Holder) == 16,
               "member layout consumes the attributed type alignment");
_Static_assert(sizeof(struct Holder) == 16,
               "record tail padding follows the attributed alignment");
_Static_assert(__builtin_types_compatible_p(aligned_pointer, int *),
               "type alignment is not a C compatibility qualifier");

int main(void)
{
    return 0;
}
