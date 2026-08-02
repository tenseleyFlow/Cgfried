// FLAGS: -fsyntax-only -Wchar-subscripts
// WARN_COUNT: 0
void signed_char_index(signed char c)
{
    int values[256];
    values[c] = 1;
}
