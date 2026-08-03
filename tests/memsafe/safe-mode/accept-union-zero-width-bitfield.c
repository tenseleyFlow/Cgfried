// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
union ZeroWidth {
    void *pointer;
    unsigned int : 0;
};
