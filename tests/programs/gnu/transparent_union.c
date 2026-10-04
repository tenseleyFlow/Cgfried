// FLAGS: -std=gnu17
/* A transparent union accepts each member type at a call site but travels on
 * the wire exactly as its first member.  Exercise scalar bits, pointer
 * bridges, aggregate ABI classification, and compatible redeclarations. */
typedef union {
  int integer;
  float real;
} IntOrFloat __attribute__((transparent_union));
typedef IntOrFloat IntOrFloatAlias;

union TaggedValue {
  int integer;
  float real;
};
typedef union TaggedValue TransparentOne __attribute__((transparent_union));
typedef union TaggedValue TransparentTwo __attribute__((transparent_union));

_Static_assert(__builtin_types_compatible_p(IntOrFloat, IntOrFloatAlias),
               "ordinary aliases retain transparent-union identity");
_Static_assert(!__builtin_types_compatible_p(TransparentOne, TransparentTwo),
               "independently attributed views have distinct identity");
_Static_assert(!__builtin_types_compatible_p(TransparentOne,
                                             union TaggedValue),
               "the unattributed tag is a distinct type view");

typedef union {
  void *generic;
  const int *integer;
} AnyPointer __attribute__((transparent_union));

struct OddAggregate {
  short x;
  short y;
  short z;
};

union __attribute__((aligned(32), transparent_union)) AlignedOddAggregate {
  struct OddAggregate value;
};

static int calls;

static int once(void) {
  calls++;
  return 37;
}

static int take_integer(IntOrFloat value) { return value.integer; }

static float take_float(IntOrFloat value) { return value.real; }

static int take_pointer(AnyPointer value) {
  return value.generic != (void *)0;
}

static int take_aggregate(union AlignedOddAggregate value) {
  return value.value.x * 100 + value.value.y * 10 + value.value.z;
}

static int redeclared(IntOrFloat);
static int redeclared(int value) { return value + 1; }

int main(void) {
  IntOrFloat object = {.integer = 19};
  union AlignedOddAggregate aggregate = {.value = {4, 2, 1}};
  int pointed = 1;

  if (take_integer(once()) != 37 || calls != 1)
    return 1;
  if (take_integer(object) != 19)
    return 2;
  if (take_float(3.25f) != 3.25f)
    return 3;
  if (take_pointer((void *)0) || !take_pointer(&pointed))
    return 4;
  if (take_aggregate(aggregate) != 421 ||
      take_aggregate((struct OddAggregate){5, 7, 3}) != 573)
    return 5;
  if (redeclared(8) != 9)
    return 6;
  return 0;
}
