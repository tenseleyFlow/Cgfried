// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
union Bad {
    int *pointer;
    unsigned long bits;
};
