// FLAGS: -fsyntax-only -pedantic
// WARNING_EXPECTED: ISO C restricts enumerator values to range of 'int'
// 6.7.2.2p2 makes an out-of-int enumerator a constraint violation, but
// gcc accepts it as an extension and only complains under -pedantic — so
// we do too, and the constant takes the enum's own type rather than int.
enum Big { small = 1, big = 3000000000 };
