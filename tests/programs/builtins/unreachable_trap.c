// __builtin_unreachable and __builtin_trap both emit ud2 today; the
// fixture pins that the instruction is REACHED (not optimized into a
// fallthrough) and that the program dies on it rather than running on.
// FLAGS: -S
// ASM_CHECK(x86_64-linux-gnu): ud2
int main(void)
{
    __builtin_trap();
    return 0;
}
