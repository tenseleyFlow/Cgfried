// FLAGS: -fsyntax-only -Wall -Wextra
// WARN_COUNT: 0

struct S {
    int member;
};

int compound_literal_parameter(int value)
{
    return (struct S){value}.member;
}
