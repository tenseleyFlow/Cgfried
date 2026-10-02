// FLAGS: -std=gnu17 -O0 -S
// ASM_CHECK(x86_64-linux-gnu): __atomic_compare_exchange
// ASM_CHECK(x86_64-linux-musl): __atomic_compare_exchange
// ASM_CHECK(x86_64-freebsd): __atomic_compare_exchange
// ASM_CHECK(arm64-linux): {{bl[ \t]+__atomic_compare_exchange}}
// ASM_CHECK(arm64-macos): {{bl[ \t]+___atomic_compare_exchange}}

typedef unsigned __int128 u128;

_Atomic(u128) object;

u128 add_value(u128 value)
{
    return object += value;
}

u128 multiply_value(u128 value)
{
    return object *= value;
}

u128 postincrement_value(void)
{
    return object++;
}
