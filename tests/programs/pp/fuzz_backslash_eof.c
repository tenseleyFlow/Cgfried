// FLAGS: -E
// CHECK: tail\
// ppfuzz seed 1954: a backslash before the newline PHASE 1 SYNTHESIZED
// (file not ending in one) is not a splice - gcc keeps the backslash.
tail\