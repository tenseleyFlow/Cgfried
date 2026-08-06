// FLAGS: -S
// The .eh_frame FDE must anchor on a LOCAL label, not on the function's
// global symbol. Naming the global emits a PC32 relocation against an
// interposable symbol, and ld refuses it outright when making a shared
// object: "relocation R_X86_64_PC32 against symbol `f' can not be used when
// making a shared object; recompile with -fPIC".
//
// So no object we produced could go into a .so, on ANY target, and nothing
// noticed because we had never built one until -shared landed. gcc emits the
// same local-label indirection for the same reason.
// ASM_CHECK: .Lfb0:
// ASM_CHECK: .long .Lfb0-.
// ASM_CHECK: .long .Lfe0_0-.Lfb0
int visible_fn(int x)
{
    return x + 1;
}
