// FLAGS: -fsyntax-only -Wchar-subscripts
// WARN_COUNT: 0
int literal_index(void)
{
    int values[256] = {0};
    return values['A'];
}
