// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
union ArrayOverlap {
    void *pointers[2];
    struct {
        void *pointer;
        unsigned long bits;
    } mixed;
};
