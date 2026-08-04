// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
struct MatchingCell1025 {
    void *pointer;
    unsigned long bits;
};

struct MatchingRow1025 {
    struct MatchingCell1025 cells[1025];
    char padding;
};

union NestedLargeMatching {
    struct MatchingRow1025 first[1025];
    struct MatchingRow1025 second[1025];
};
