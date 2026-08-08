// FLAGS: -S -fPIE
// A const object whose initializer holds an ADDRESS cannot go in .rodata once
// the image is position-independent: the loader has to write that word at
// startup, and a read-only page is the one place it cannot. .data.rel.ro is
// the standard answer -- writable while relocations are applied, then
// mprotected read-only for the rest of the process (RELRO).
//
// Without PIC the same object DOES belong in .rodata, because the address is
// a link-time constant and nothing writes it at run time. That half is pinned
// by const_rodata_nopic.c; the two together are the whole rule, and a
// compiler that got only one right would look correct in whichever mode it
// was tested in.
//
// This is an assembly check rather than a runtime one on purpose: both
// sections are readable and hold the right bytes, so an executing program
// cannot tell them apart. What differs is whether the dynamic linker is
// handed a page it must write, which shows up at load time or not at all.
extern int target;
int target;

// ASM_CHECK: .section	.data.rel.ro,"aw",@progbits
const int *const with_reloc = &target;

// ASM_CHECK: .section	.rodata
const int without_reloc = 7;
