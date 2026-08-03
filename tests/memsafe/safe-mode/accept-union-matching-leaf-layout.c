// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
union Matching {
    struct {
        void *pointer;
        unsigned long bits;
    } first;
    struct {
        void *pointer;
        unsigned long bits;
    } second;
};
