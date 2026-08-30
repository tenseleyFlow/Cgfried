// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: ISO C forbids casts to union type
// WARN_COUNT: 2
union U {
  int integer;
  char *pointer;
};

void cast_values(int integer, char *pointer) {
  // WARN_CHECK: pedantic ISO C forbids casts to union type
  (union U) integer;
  (union U) pointer;
  __extension__(union U) integer;
  __extension__(union U) pointer;
}
