// EXIT_CODE: 0
// Interning an external symbol for a relocation must not mark its later
// definition as already emitted.  musl exposes this shape through globals
// whose address is used before their defining declaration is lowered.
extern int target;
static int *pointer = &target;
int target = 37;

int main(void)
{
    return pointer == &target && *pointer == 37 ? 0 : 1;
}
