// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
struct PointerFirst1025 {
    void *pointer;
    unsigned long bits;
};

struct PointerSecond1025 {
    unsigned long bits;
    void *pointer;
};

struct PointerFirstRow1025 {
    struct PointerFirst1025 cells[1025];
    char padding;
};

struct PointerSecondRow1025 {
    struct PointerSecond1025 cells[1025];
    char padding;
};

union NestedLargeCrossed {
    struct PointerFirstRow1025 first[1025];
    struct PointerSecondRow1025 second[1025];
};
