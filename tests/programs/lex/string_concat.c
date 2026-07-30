// FLAGS: --dump-tokens
// CHECK: STRING 7 bytes: 68 69 74 68 65 72 65
// CHECK: STRING L8 bytes: 61 00 00 00 62 00 00 00
// CHECK: STRING 2 bytes: 12 33
// Phase 6: plain + prefixed adopts the prefix; escapes are decoded PER
// LITERAL first, so "\x12" "3" is two bytes, never \x123.
"hi" "there"
;
"a" L"b"
;
"\x12" "3"
