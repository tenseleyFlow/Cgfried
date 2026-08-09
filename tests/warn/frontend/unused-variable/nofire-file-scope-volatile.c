// FLAGS: -S -Wunused-variable
// WARN_COUNT: 0
/* gcc exempts a FILE-SCOPE volatile object from -Wunused-variable: the
 * declaration IS the point (it names a hardware register or a location some
 * other agent writes), so "defined but not used" is never the right advice.
 * The exemption does NOT extend to a local volatile -- see fire-local-volatile
 * -- and both halves were measured against gcc 8 and gcc 16 before this
 * landed, because implementing only the half you tested is how an exemption
 * turns into a hole. */
static volatile int hw_register;
