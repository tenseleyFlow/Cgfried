// FLAGS: -fPIE -S
// SKIP(arm64-macos): external ELF TLS initial-exec is not a Mach-O ABI model
// ASM_CHECK(x86_64-linux-gnu): {{movq[ \t]+elsewhere@GOTTPOFF\(%rip\), %r}}
// ASM_CHECK(x86_64-linux-gnu): {{movq[ \t]+%fs:0, %r}}
// ASM_CHECK(arm64-linux): {{adrp[ \t]+x[0-9]+, :gottprel:elsewhere}}
// ASM_CHECK(arm64-linux): {{ldr[ \t]+x[0-9]+, \[x[0-9]+, :gottprel_lo12:elsewhere\]}}
// PIE executables still have a fixed initial TLS block, so external TLS uses
// initial-exec here. Full -fPIC is the separate general-dynamic boundary.
extern _Thread_local int elsewhere;

int read_it(void) { return elsewhere; }
void write_it(int value) { elsewhere = value; }
int *address_of_it(void) { return &elsewhere; }
