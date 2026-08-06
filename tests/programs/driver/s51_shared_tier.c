// FLAGS: -shared
// ERROR_EXPECTED: -shared needs -fPIC
// The -shared tier is decided rather than discovered at run time: -fPIC
// codegen works on both architectures and -shared through the SYSTEM linker
// works, because ld does the dynamic-section heavy lifting. afs-ld's dynamic
// ELF lane is out for v0.1.0 and Mach-O dylib emission is out entirely; each
// says so by name.
//
// gcc warns and proceeds here, producing a shared object with text
// relocations that may or may not load. Ours is an error: the whole point of
// the flag is code that can be mapped anywhere.
int lib_fn(void)
{
    return 1;
}
