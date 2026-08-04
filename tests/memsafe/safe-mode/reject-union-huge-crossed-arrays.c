// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
union HugeCrossed {
    void *pointers[10000000];
    unsigned long bits[10000000];
};
