// FLAGS: -fsyntax-only -Woverflow
// WARN_COUNT: 0

int overflow_signed_bit_pattern = 2147483648UL;
int overflow_ioctl_shape =
    (2U << 30) | ('T' << 8) | 0x14 | (sizeof(char[96]) << 16);
