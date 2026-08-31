// A runtime-index offsetof is an ordinary GNU expression, not an integer
// constant expression. Constant indices in the companion fixture remain ICEs.
// ERROR_EXPECTED: not a constant expression

struct S {
    int values[4];
};
int index_value;
enum {
    BAD_OFFSET = __builtin_offsetof(struct S, values[index_value])
};
