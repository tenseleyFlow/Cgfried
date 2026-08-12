// EXIT_CODE: 0
// A weak hidden declaration has no IrGlobal definition, but its assembler
// attributes are still load-bearing: the linker resolves the absent object
// to address zero. Musl uses this exact contract for its nullable _DYNAMIC.
extern int absent_object __attribute__((weak, visibility("hidden")));

int main(void)
{
    return &absent_object != 0;
}
