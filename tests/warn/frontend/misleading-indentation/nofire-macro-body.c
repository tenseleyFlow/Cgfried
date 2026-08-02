// FLAGS: -fsyntax-only -Wmisleading-indentation
// WARN_COUNT: 0

#define SET_ONE(pointer) *pointer = 1

void misleading_macro_body(int condition, int *value)
{
    if (condition)
        SET_ONE(value);
        *value = 2;
}
