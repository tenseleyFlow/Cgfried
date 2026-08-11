// FLAGS: --target=arm64-macos -O2 -S
// Mach-O relocation modifiers bind to the symbol before the addend.  This
// spelling is accepted by both Apple as and the bundled afs-as.
// ASM_CHECK: adrp x{{[0-9]+}}, _data@PAGE+56
// ASM_CHECK: add x{{[0-9]+}}, x{{[0-9]+}}, _data@PAGEOFF+56
// ASM_CHECK: adrp x{{[0-9]+}}, _data@PAGE+4096
// ASM_CHECK: add x{{[0-9]+}}, x{{[0-9]+}}, _data@PAGEOFF+4096
// ASM_CHECK-NOT: _data+56@PAGE
// ASM_CHECK-NOT: _data+4096@PAGE

static long data[513];

long *small_offset(void)
{
    return &data[7];
}

long *page_offset(void)
{
    return &data[512];
}
