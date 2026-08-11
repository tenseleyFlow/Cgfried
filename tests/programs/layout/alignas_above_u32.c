// FLAGS: -fsyntax-only
// ERROR_EXPECTED: maximum supported alignment of 16777216 bytes
_Alignas(4294967296LL) int above_u32;
