// FLAGS: -fsyntax-only
// ERROR_EXPECTED: weaker than the natural alignment
_Alignas(1) int x;
