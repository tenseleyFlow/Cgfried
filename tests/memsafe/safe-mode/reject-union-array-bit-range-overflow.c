// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
union HugeBitRange {
    char bytes[1152921504606846977ULL];
    void *pointer;
};
