// FLAGS: -fsyntax-only
// Every one of these is a valid integer constant expression, and each
// exercises a rule that would be easy to get wrong: short-circuit and the
// untaken conditional branch mean a division by zero is NOT evaluated;
// sizeof folds through layout; a float is allowed as the immediate
// operand of an integer cast.
enum E { A, B = 5, C, D = B * 2 + 1 };
_Static_assert(D == 11, "arithmetic");
_Static_assert(C == 6, "implicit previous + 1");
_Static_assert(sizeof(int) * 4 == 16, "sizeof");
_Static_assert(_Alignof(double) == 8, "alignof");
_Static_assert((int)3.9 == 3, "float to int truncates toward zero");
_Static_assert(1 ? 1 : 1 / 0, "the untaken branch is not evaluated");
_Static_assert(!(0 && (1 / 0)), "&& short-circuits");
_Static_assert(1 || (1 / 0), "|| short-circuits");
_Static_assert((1 << 10) - 1 == 1023, "shifts");
_Static_assert(-7 / 2 == -3, "signed division truncates toward zero");
_Static_assert(-7 % 2 == -1, "and so does the remainder");
_Static_assert(4000000000u + 1000000000u == 705032704u, "unsigned wraps");
int arr[B + 2];
_Static_assert(sizeof(arr) == 28, "array bounds fold");
