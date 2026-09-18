// GCC's __builtin_classify_type is an integer constant expression over the
// converted operand type. Its operand is typed but never evaluated.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}touch
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}touch

struct S {
    int value;
};

union U {
    int integer;
    double real;
};

enum E { E0 };
typedef int ti __attribute__((mode(TI)));

static int side_effects;

static int __attribute__((warn_unused_result)) touch(void)
{
    side_effects++;
    return 1;
}

static int function(double value)
{
    return (int)value;
}

_Static_assert(__builtin_classify_type((_Bool)0) == 1, "bool is integer");
_Static_assert(__builtin_classify_type((char)0) == 1, "char is integer");
_Static_assert(__builtin_classify_type((enum E)0) == 1, "enum is integer");
_Static_assert(__builtin_classify_type((ti)0) == 1, "TI is integer");
_Static_assert(__builtin_classify_type((int *)0) == 5, "pointer");
_Static_assert(__builtin_classify_type(1.0f) == 8, "float is real");
_Static_assert(__builtin_classify_type(1.0) == 8, "double is real");
_Static_assert(__builtin_classify_type(1.0L) == 8, "long double is real");
_Static_assert(__builtin_classify_type((struct S){0}) == 12, "struct");
_Static_assert(__builtin_classify_type((union U){0}) == 13, "union");
_Static_assert(__builtin_classify_type(void) == 0, "void type");
_Static_assert(__builtin_classify_type(_Bool) == 4, "bool type");
_Static_assert(__builtin_classify_type(enum E) == 3, "enum type");
_Static_assert(__builtin_classify_type(int (void)) == 10, "function type");
_Static_assert(__builtin_classify_type(int [2]) == 14, "array type");

int main(void)
{
    int array[2] = {0};
    int before = side_effects;

    if (__builtin_classify_type(function) != 5)
        return 1;
    if (__builtin_classify_type(array) != 5)
        return 2;
    if (__builtin_classify_type(touch()) != 1)
        return 3;
    if (__builtin_classify_type(side_effects++) != 1)
        return 4;
    if (side_effects != before)
        return 5;
    if (__builtin_classify_type(int [touch()]) != 14)
        return 6;
    if (side_effects != before)
        return 7;
    return 0;
}
