// FLAGS: -std=gnu17 -O0 -S
// ASM_CHECK(x86_64-linux-gnu): __atomic_store_16
// ASM_CHECK(x86_64-linux-gnu): __atomic_load_16
// ASM_CHECK(x86_64-linux-musl): __atomic_store_16
// ASM_CHECK(x86_64-linux-musl): __atomic_load_16
// ASM_CHECK(x86_64-freebsd): __atomic_store_16
// ASM_CHECK(x86_64-freebsd): __atomic_load_16
// ASM_CHECK(arm64-linux): {{bl[ \t]+__atomic_store}}
// ASM_CHECK(arm64-linux): {{bl[ \t]+__atomic_load}}
// ASM_CHECK(arm64-macos): {{ldp[ \t]+x12, x13, \[x[0-9]+\]}}
// ASM_CHECK(arm64-macos): {{dmb[ \t]+ish}}
// ASM_CHECK(arm64-macos): {{mov[ \t]+v[0-9]+\.d\[1\], x13}}
// ASM_CHECK(arm64-macos): {{dmb[ \t]+ish}}
// ASM_CHECK(arm64-macos): {{umov[ \t]+x12, v[0-9]+\.d\[0\]}}
// ASM_CHECK(arm64-macos): {{stp[ \t]+x12, x13, \[x[0-9]+\]}}

typedef unsigned __int128 u128;

_Atomic(u128) object;

u128 load_value(void)
{
    return object;
}

u128 store_value(u128 value)
{
    return object = value;
}
