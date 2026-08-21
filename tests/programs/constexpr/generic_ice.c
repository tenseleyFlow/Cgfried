// FLAGS: -fsyntax-only
// A generic selection has the constant-expression status of its selected
// association. Calls in the controlling expression or unselected associations
// must never be evaluated.
int runtime_value(void);

enum {
    EXPLICIT = _Generic((unsigned int)runtime_value(),
                        unsigned int: 7,
                        default: runtime_value()),
    DEFAULTED = _Generic((long)runtime_value(),
                         int: runtime_value(),
                         default: 5),
    NESTED = _Generic(0,
                      int: _Generic((double)runtime_value(),
                                    double: 9,
                                    default: runtime_value()),
                      default: runtime_value())
};

_Static_assert(EXPLICIT == 7, "explicit association is an ICE");
_Static_assert(DEFAULTED == 5, "default association is an ICE");
_Static_assert(NESTED == 9, "nested generic selection is an ICE");

struct generic_bitfield {
    unsigned value : _Generic(0, int: 3, default: runtime_value());
};

int generic_bound[_Generic(0, int: 4, default: runtime_value())];
_Static_assert(sizeof(generic_bound) == 4 * sizeof(int),
               "generic selection folds in an array bound");
