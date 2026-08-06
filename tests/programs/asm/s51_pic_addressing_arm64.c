// FLAGS: --target=arm64-linux -fPIE -O1 -S
// The PIE row does NOT agree between the architectures, and this fixture
// exists to pin that. aarch64 gcc routes an UNDEFINED extern through the GOT
// under -fPIE; x86 gcc emits plain (%rip) for the same input. Generalising
// from either one to the other would have been wrong, so both were measured.
//
// A defined global stays direct under PIE on both. Full PIC sends both
// through the GOT on both -- that row does agree.
//
// Calls need nothing here: aarch64 keeps a plain `bl` and the CALL26
// relocation already permits the linker to insert a PLT stub, which is the
// same reason Sprint 49 stopped resolving branches to .globl symbols.
// ASM_CHECK: adrp x{{[0-9]+}}, :got:ext_var
// ASM_CHECK: ldr x{{[0-9]+}}, [x{{[0-9]+}}, #:got_lo12:ext_var]
// ASM_CHECK: adrp x{{[0-9]+}}, def_var
extern int ext_var;
int def_var = 7;

int *addr_ext(void)
{
    return &ext_var;
}

int *addr_def(void)
{
    return &def_var;
}
