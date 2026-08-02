// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0

struct S {
    int member;
};

static const int value = 1;

int static_compound_literal_read(void)
{
    return ((struct S){value}).member;
}
