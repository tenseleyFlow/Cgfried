// FLAGS: -S
// The other half of const_data_rel_ro.c: WITHOUT position independence, an
// address in an initializer is resolved by the linker and never written
// again, so the object stays in .rodata. Emitting .data.rel.ro here would
// give up read-only storage for a relocation that does not exist at run time.
//
// The const tentative definition is the row worth pinning: .comm cannot ask
// for read-only storage, so a const object never goes through it. gcc gives
// `const int tentative;` a real .rodata definition too.
extern int target;
int target;

// ASM_CHECK: .section	.rodata
const int *const with_reloc = &target;

// ASM_CHECK: .section	.rodata
const int tentative;
