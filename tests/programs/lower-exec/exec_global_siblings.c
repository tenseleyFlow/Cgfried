// EXIT_CODE: 0
// File-scope comma siblings are separate definitions. QBE declares both its
// global instruction cursor and a static edge table this way; registering the
// symbols in sema without emitting their storage produced link-time undefined
// references.
int primary, sibling;
static int *first, *second;

int main(void)
{
    sibling = 7;
    second = &sibling;
    return primary == 0 && first == 0 && *second == 7 ? 0 : 1;
}
