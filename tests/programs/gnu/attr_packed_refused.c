// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: a bit-field in a packed struct is not yet supported
//
// Rule 5 of .docs/audits/packed-layout.md -- packed bit-fields allocate
// bit-contiguously with no storage-unit alignment -- is NOT implemented, and
// half of packed applied silently is exactly the failure mode the tier table
// exists to prevent: a bit-field-bearing packed struct would get the wrong
// layout with no diagnostic. It refuses by name until rule 5 lands.
struct WithBitfield {
    char a;
    int b : 3;
} __attribute__((packed));

// Ordinary unaligned loads and stores are fine on both targets. The exclusive
// instructions that implement _Atomic on arm64 are not, so an _Atomic member
// of a packed struct is refused rather than silently made non-atomic.
struct WithAtomic {
    char a;
    _Atomic int b;
} __attribute__((packed));

int main(void)
{
    return 0;
}
