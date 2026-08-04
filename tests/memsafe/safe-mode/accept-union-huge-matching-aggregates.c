// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
struct MatchingCell {
    void *pointer;
    unsigned long bits;
};

union HugeMatching {
    struct MatchingCell first[50000];
    struct MatchingCell second[50000];
};
