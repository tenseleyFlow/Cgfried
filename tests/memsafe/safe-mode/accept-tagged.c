// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
struct Tagged {
    int kind;
    int *pointer;
    unsigned long bits;
};
int accept_tagged(struct Tagged *value)
{
    return value->kind;
}
