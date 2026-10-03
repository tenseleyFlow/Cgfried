// FLAGS: -std=gnu17 -O0 -S
// ASM_CHECK(x86_64-linux-gnu): {{lock[ \t]+cmpxchgl}}
// ASM_CHECK(x86_64-linux-gnu): {{lock[ \t]+cmpxchgq}}
// ASM_CHECK(x86_64-linux-musl): {{lock[ \t]+cmpxchgl}}
// ASM_CHECK(x86_64-linux-musl): {{lock[ \t]+cmpxchgq}}
// ASM_CHECK(x86_64-freebsd): {{lock[ \t]+cmpxchgl}}
// ASM_CHECK(x86_64-freebsd): {{lock[ \t]+cmpxchgq}}
// ASM_CHECK(arm64-linux): {{ldaxr[ \t]+w}}
// ASM_CHECK(arm64-linux): {{stlxr[ \t]+w[0-9]+, w}}
// ASM_CHECK(arm64-linux): {{ldaxr[ \t]+x}}
// ASM_CHECK(arm64-linux): {{stlxr[ \t]+w[0-9]+, x}}
// ASM_CHECK(arm64-macos): {{ldaxr[ \t]+w}}
// ASM_CHECK(arm64-macos): {{stlxr[ \t]+w[0-9]+, w}}
// ASM_CHECK(arm64-macos): {{ldaxr[ \t]+x}}
// ASM_CHECK(arm64-macos): {{stlxr[ \t]+w[0-9]+, x}}

struct AtomicRecord {
    _Atomic(float) f[1];
    _Atomic(double) d[1];
} __attribute__((scalar_storage_order("big-endian")));

static struct AtomicRecord record;

float add_float(float value)
{
    return record.f[0] += value;
}

double postincrement_double(void)
{
    return record.d[0]++;
}
