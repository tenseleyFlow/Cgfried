// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
struct PointerFirst {
    void *pointer;
    unsigned long bits;
};

struct PointerSecond {
    unsigned long bits;
    void *pointer;
};

union HugeCrossedAggregates {
    struct PointerFirst first[50000];
    struct PointerSecond second[50000];
};
