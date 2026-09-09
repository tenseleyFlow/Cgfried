// FLAGS: -O0 -S --target=x86_64-linux-gnu
// An out-of-range shift is undefined in C, but the x86 instruction itself
// masks the count. Keep the immediate path assembler-valid and consistent
// with the existing %cl path instead of printing an unencodable literal.
// ASM_CHECK: shll{{[ \t]+}}$13,
// ASM_CHECK: shrl{{[ \t]+}}$13,
// ASM_CHECK: sarq{{[ \t]+}}$45,
// ASM_CHECK-NOT: $3737046253

int x64_shift_left_32(int value)
{
    return value << 0xdebecced;
}

unsigned x64_shift_right_32(unsigned value)
{
    return value >> 0xdebecced;
}

long x64_shift_right_64(long value)
{
    return value >> 0xdebeccedL;
}
