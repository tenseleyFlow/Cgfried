// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: duplicate case value
void f(int x)
{
    switch (x) {
    case 10 ... 20:
        break;
    case 15:
        break;
    }
}
