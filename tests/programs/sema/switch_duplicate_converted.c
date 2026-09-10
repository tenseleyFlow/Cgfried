// FLAGS: -fsyntax-only
// ERROR_EXPECTED: duplicate case value
void f(unsigned int x)
{
    switch (x) {
    case -1:
        break;
    case 0xffffffffu:
        break;
    }
}
