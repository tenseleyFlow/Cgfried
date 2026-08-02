// FLAGS: -fsyntax-only -Wchar-subscripts
// WARN_COUNT: 1
void char_index(char c)
{
    int values[256];
    // WARN_CHECK: char-subscripts array subscript has type 'char'
    values[c] = 1;
}
