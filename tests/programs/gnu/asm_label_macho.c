// FLAGS: -std=gnu17 --target=arm64-macos -S
// ASM_CHECK: {{^renamed_fn:}}
// ASM_CHECK: {{^_macro_fn:}}
// ASM_CHECK: {{^_ordinary:}}
// ASM_CHECK: {{^collision:}}
// ASM_CHECK: {{^_collision:}}
// ASM_CHECK-NOT: {{^_renamed_fn:}}
// ASM_CHECK-NOT: {{^__macro_fn:}}

#define STR_RAW(x) #x
#define STR(x) STR_RAW(x)

int renamed(void) __asm__("renamed_fn");
int macro_named(void) __asm__(STR(__USER_LABEL_PREFIX__) "macro_fn");
int exact_collision __asm__("collision") = 3;
int collision = 4;

int renamed(void)
{
    return 1;
}

int macro_named(void)
{
    return 2;
}

int ordinary(void)
{
    return renamed() + macro_named() + exact_collision + collision;
}
