// FLAGS: -fsyntax-only
// ERROR_EXPECTED: maximum supported alignment of 16777216 bytes
_Alignas(33554432) int too_large;
