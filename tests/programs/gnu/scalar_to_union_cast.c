// FLAGS: -std=gnu17
/* GNU casts to a union select an exact member type and materialize a union
 * rvalue. Cover scalar, pointer, aggregate, static-image, call/return, and
 * single-evaluation paths. */
struct Pair {
  int x;
  int y;
};

union Value {
  int integer;
  char *pointer;
  struct Pair pair;
  double real;
};

union Duplicate {
  int first;
  int second;
};

static char static_text[] = "union";
static union Value static_integer = (union Value)0x12345678;
static union Value static_pointer = (union Value)static_text;
static int calls;

static char *pointer_once(void) {
  calls++;
  return static_text;
}

static int take_integer(union Value value) { return value.integer; }

static struct Pair take_pair(union Value value) { return value.pair; }

int main(void) {
  const int qualified = 31;
  union Value integer = (union Value)qualified;
  union Value pointer;
  union Value pair = (union Value)(struct Pair){11, 13};
  union Duplicate duplicate = (union Duplicate)17;
  struct Pair returned;

  pointer = (union Value)(1 ? (char *)0 : pointer_once());
  if (pointer.pointer || calls != 0)
    return 1;
  pointer = (union Value)(0 ? (char *)0 : pointer_once());
  if (pointer.pointer != static_text || calls != 1)
    return 2;
  if (integer.integer != 31 || take_integer((union Value)41) != 41)
    return 3;
  returned = take_pair(pair);
  if (returned.x != 11 || returned.y != 13)
    return 4;
  if (duplicate.first != 17 || duplicate.second != 17)
    return 5;
  if (static_integer.integer != 0x12345678 ||
      static_pointer.pointer != static_text)
    return 6;
  return 0;
}
