// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// clang-format off
// WARN_CHECK: int-conversion initialization to 'int *' from 'long'
#define NESTED_ORDERED_PRAGMA() int *before = 1L; IGNORE_INT_CONVERSION int *after = 2;
#define IGNORE_INT_CONVERSION _Pragma("GCC diagnostic ignored \"-Wint-conversion\"")
// clang-format on
void pragma_macro_nested_order(void)
{
    NESTED_ORDERED_PRAGMA()
}
