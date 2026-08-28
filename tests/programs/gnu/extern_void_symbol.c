// FLAGS: -std=gnu17 -fdump-init
// CHECK: address: size=8 bytes=0000000000000000 reloc@0=marker-268435457

extern void marker;
static __SIZE_TYPE__ address = (__SIZE_TYPE__)&marker - 0x10000000L - 1;
