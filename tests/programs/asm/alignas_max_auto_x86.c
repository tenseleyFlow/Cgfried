// FLAGS: -S
// ASM_CHECK: max_fixed:
// ASM_CHECK: max_dynamic:
// The implementation-defined extended-alignment ceiling must compile through
// the complete x86 backend for both fixed and variably modified objects.
int max_fixed(void)
{
    _Alignas(16777216) unsigned char object;

    return ((unsigned long)&object & 16777215ul) != 0;
}

int max_dynamic(int n)
{
    _Alignas(16777216) unsigned char object[n];

    return ((unsigned long)object & 16777215ul) != 0;
}
