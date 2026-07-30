// FLAGS: -E
// ERROR_EXPECTED: GNU named variadic
// The last preprocessor gap: #define M(args...) — Sprint 55.
#define M(args...) args
M(1, 2)
