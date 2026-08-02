// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// clang-format off
// WARN_CHECK: int-conversion initialization to 'int *' from 'long'
#define ORDERED_PRAGMA() int *before = 1L; _Pragma("GCC diagnostic ignored \"-Wint-conversion\"") int *after = 2;
// clang-format on
void pragma_macro_order(void)
{
    ORDERED_PRAGMA()
}
