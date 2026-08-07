// FLAGS: -S
// ASM_CHECK(x86_64-linux-gnu): {{^\s*\.weak\s+weak_fn}}
// ASM_CHECK(x86_64-linux-gnu): {{^\s*\.hidden\s+hidden_fn}}
// ASM_CHECK(x86_64-linux-gnu): {{^\s*\.weak\s+weak_obj}}
// ASM_CHECK(x86_64-linux-gnu): {{^\s*\.hidden\s+hidden_obj}}
// The directives themselves, on both a function and an object, because the
// two travel different paths through the emitter -- and the object path is
// where the arm64 port was wrong until a readelf comparison against
// aarch64-linux-gnu-gcc caught it.
//
// `.weak` REPLACES `.globl` rather than joining it: gas takes whichever came
// last, so emitting both works in one order and silently does not in the
// other. attr_weak_overridden.c pins what it MEANS.
__attribute__((weak)) int weak_fn(void)
{
    return 1;
}

__attribute__((visibility("hidden"))) int hidden_fn(void)
{
    return 2;
}

int weak_obj __attribute__((weak)) = 3;
int hidden_obj __attribute__((visibility("hidden"))) = 4;
