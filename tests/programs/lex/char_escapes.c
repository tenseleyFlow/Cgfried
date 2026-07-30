// FLAGS: --dump-tokens
// CHECK: CHAR_CONST 97 int
// CHECK: CHAR_CONST 10 int
// CHECK: CHAR_CONST 21300 int
// CHECK: CHAR_CONST -1 int
// CHECK: CHAR_CONST 24930 int
// '\1234' is '\123' then '4' (octal takes at most 3 digits) packed
// big-endian; '\xff' is -1 because plain char is signed on this target.
'a' '\n' '\1234' '\xff' 'ab'
