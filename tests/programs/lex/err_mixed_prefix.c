// FLAGS: --dump-tokens
// ERROR_EXPECTED: concatenation of differently-prefixed string literals
L"x" u"y"
