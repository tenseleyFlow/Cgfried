// FLAGS: -std=gnu17 -fmax-errors=0
// ERROR_EXPECTED: specified mode too small for enumerated values
// ERROR_EXPECTED: specified mode too small for enumerated values
typedef enum { TOO_HIGH = 256 } __attribute__((mode(QI))) TooHigh;
typedef enum { TOO_LOW = -129 } __attribute__((mode(QI))) TooLow;
typedef unsigned __int128 u128;
typedef enum { TOO_WIDE = (u128)1 << 100 } __attribute__((mode(DI))) TooWide;
