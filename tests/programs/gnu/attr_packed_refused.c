// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: an _Atomic member of a packed struct is not supported

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
