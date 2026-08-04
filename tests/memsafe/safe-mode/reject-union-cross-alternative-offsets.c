// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
union Crossed {
    struct {
        void *pointer;
        unsigned long bits;
    } first;
    struct {
        unsigned long bits;
        void *pointer;
    } second;
};
