// FLAGS: -fdump-sema
// CHECK: TAG Fits: enum Fits [underlying int]
// CHECK: TAG Unsigned: enum Unsigned [underlying unsigned int]
// CHECK: TAG Long: enum Long [underlying long]
// CHECK: TAG Signed: enum Signed [underlying long]
// CHECK: ENUMCONST big = 3000000000: unsigned int
// gcc's ladder: the first of int, unsigned int, long, unsigned long that
// represents every enumerator. A negative value forces a signed choice
// even where the positive range would fit unsigned. Constants have type
// int UNLESS they do not fit, in which case gcc's extension gives them
// the enum's own type — observable as sizeof.
enum Fits { f1 = 1 };
enum Unsigned { small = 1, big = 3000000000 };
enum Long { l1 = 5000000000 };
enum Signed { s1 = -1, s2 = 3000000000 };
