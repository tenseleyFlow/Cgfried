// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
union HugeSafe {
    void *first[10000000];
    void *second[10000000];
};
