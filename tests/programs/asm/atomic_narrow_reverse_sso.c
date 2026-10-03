// FLAGS: -std=gnu17 -O0 -S
// ASM_CHECK(x86_64-linux-gnu): {{lock[ \t]+cmpxchgl}}
// ASM_CHECK(x86_64-linux-musl): {{lock[ \t]+cmpxchgl}}
// ASM_CHECK(x86_64-freebsd): {{lock[ \t]+cmpxchgl}}
// ASM_CHECK(arm64-linux): {{ldaxr[ \t]+w}}
// ASM_CHECK(arm64-linux): {{stlxr[ \t]+w}}
// ASM_CHECK(arm64-macos): {{ldaxr[ \t]+w}}
// ASM_CHECK(arm64-macos): {{stlxr[ \t]+w}}

struct AtomicRecord {
    _Atomic(unsigned) values[2];
} __attribute__((scalar_storage_order("big-endian")));

struct AtomicRecord record;

unsigned postincrement_value(void)
{
    return record.values[1]++;
}

unsigned multiply_value(unsigned value)
{
    return record.values[0] *= value;
}
