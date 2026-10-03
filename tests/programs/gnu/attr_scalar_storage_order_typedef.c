// FLAGS: -fsyntax-only -std=gnu17
#define BE __attribute__((scalar_storage_order("big-endian")))
#define LE __attribute__((scalar_storage_order("little-endian")))

struct S {
    unsigned int value;
};

typedef struct S AttributedS BE;
typedef AttributedS AliasS;
typedef struct S IndependentS BE;
typedef AttributedS RetaggedS LE;

_Static_assert(__builtin_types_compatible_p(AttributedS, AliasS),
               "an ordinary alias preserves the attributed type view");
_Static_assert(!__builtin_types_compatible_p(AttributedS, struct S),
               "the attributed typedef is distinct from the plain tag");
_Static_assert(!__builtin_types_compatible_p(AttributedS, IndependentS),
               "independently attributed views are distinct");
_Static_assert(__builtin_types_compatible_p(AttributedS, RetaggedS),
               "re-attributing an existing view retains its identity");
_Static_assert(sizeof(AttributedS) == sizeof(struct S),
               "the representation changes byte order, not extent");
