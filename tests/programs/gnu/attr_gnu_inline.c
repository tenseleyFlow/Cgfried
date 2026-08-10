// FLAGS: -S -std=gnu17
// ASM_CHECK: {{^gnu_plain:}}
// ASM_CHECK-NOT: {{^gnu_extern:}}

int gnu_extern(void);

extern inline __attribute__((gnu_inline)) int gnu_extern(void)
{
    return 17;
}

inline __attribute__((gnu_inline)) int gnu_plain(void)
{
    return 23;
}
