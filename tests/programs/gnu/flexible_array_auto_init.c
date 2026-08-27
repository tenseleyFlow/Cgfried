// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: non-static initialization of a flexible array member
struct S {
    int head;
    int tail[];
};

void f(void)
{
    struct S automatic = {1, {2}};

    (void)automatic;
}
