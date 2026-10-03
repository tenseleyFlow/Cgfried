// FLAGS: -std=gnu17 -O0 -S
// ASM_CHECK(x86_64-linux-gnu): __atomic_load_16
// ASM_CHECK(x86_64-linux-gnu): __atomic_store_16
// ASM_CHECK(x86_64-linux-gnu): __atomic_compare_exchange
// ASM_CHECK(x86_64-linux-musl): __atomic_load_16
// ASM_CHECK(x86_64-linux-musl): __atomic_store_16
// ASM_CHECK(x86_64-linux-musl): __atomic_compare_exchange
// ASM_CHECK(x86_64-freebsd): __atomic_load_16
// ASM_CHECK(x86_64-freebsd): __atomic_store_16
// ASM_CHECK(x86_64-freebsd): __atomic_compare_exchange
// ASM_CHECK(arm64-linux): {{bl[ \t]+__atomic_load}}
// ASM_CHECK(arm64-linux): {{bl[ \t]+__atomic_store}}
// ASM_CHECK(arm64-linux): {{bl[ \t]+__atomic_compare_exchange}}
// ASM_CHECK(arm64-macos): {{ldp[ \t]+x12, x13, \[x[0-9]+\]}}
// ASM_CHECK(arm64-macos): {{dmb[ \t]+ish}}
// ASM_CHECK(arm64-macos): {{stp[ \t]+x12, x13, \[x[0-9]+\]}}
// ASM_CHECK(arm64-macos): {{bl[ \t]+___atomic_compare_exchange}}

typedef unsigned __int128 u128;

struct AtomicRecord {
    _Atomic(u128) value;
    _Atomic(u128) values[2];
} __attribute__((scalar_storage_order("big-endian")));

struct AtomicRecord record;

u128 load_value(void)
{
    return record.value;
}

u128 store_value(u128 value)
{
    return record.value = value;
}

u128 add_value(u128 value)
{
    return record.values[1] += value;
}
