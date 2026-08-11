// FLAGS: --target=arm64-linux -S
// ASM_CHECK: max_fixed_arm64:
// ASM_CHECK: max_dynamic_arm64:
// The common extended-alignment ceiling must also survive ARM frame lowering;
// its frame and address adjustments can require more than two immediates.
int max_fixed_arm64(void)
{
    _Alignas(16777216) unsigned char object;

    return ((unsigned long)&object & 16777215ul) != 0;
}

int max_dynamic_arm64(int n)
{
    _Alignas(16777216) unsigned char object[n];

    return ((unsigned long)object & 16777215ul) != 0;
}
