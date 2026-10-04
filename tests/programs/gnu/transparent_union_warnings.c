// FLAGS: -fsyntax-only -std=gnu17
// WARNING_EXPECTED: transparent_union
// WARN_COUNT: 5
/* GCC warns and ignores a transparent-union request whose first member does
 * not have the union's machine representation, whose first member uses a
 * floating mode, or whose spelling is attached to a parameter/object rather
 * than to a union type. */
// WARN_CHECK: attributes union cannot be made transparent
union BadRepresentation {
  char first;
  int larger[2];
} __attribute__((transparent_union));

struct EightByteAggregate {
  int first;
  int second;
};

// WARN_CHECK: attributes union cannot be made transparent
union __attribute__((aligned(32), transparent_union)) BadAlignedMode {
  struct EightByteAggregate first;
};

// WARN_CHECK: attributes 'transparent_union' attribute ignored
typedef union { float first; int bits; } Bad __attribute__((transparent_union));

union Ordinary {
  int value;
};

// WARN_CHECK: attributes 'transparent_union' attribute ignored on a function parameter
void misplaced_parameter(union Ordinary value __attribute__((transparent_union)));

// WARN_CHECK: attributes 'transparent_union' attribute ignored
union Ordinary misplaced_object __attribute__((transparent_union));
