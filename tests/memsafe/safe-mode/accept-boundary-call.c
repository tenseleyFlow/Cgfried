// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
void unsafe_asm_boundary(void);
void accept_boundary_call(void)
{
    unsafe_asm_boundary();
}
