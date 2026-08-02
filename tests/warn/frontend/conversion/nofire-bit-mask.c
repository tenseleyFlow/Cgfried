// FLAGS: -fsyntax-only -Wconversion
// WARN_COUNT: 0
unsigned char conversion_mask(unsigned int value) { return value & 0xff; }
