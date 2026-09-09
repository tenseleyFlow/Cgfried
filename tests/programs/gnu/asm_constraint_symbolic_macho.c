// FLAGS: -S -O2 -std=gnu17
// ASM_CHECK(arm64-macos): # symbolic-macho ordinary=_ordinary exact=renamed$exact string=l_str.0
// Mach-O prefixes ordinary C symbols, preserves declaration asm labels
// verbatim, and gives relocation-bearing anonymous strings private `l_`
// names. Symbolic asm constants must follow those same linker spellings.
int ordinary;
int exact __asm__("renamed$exact");

void symbolic_macho_names(void)
{
    __asm__ volatile("# symbolic-macho ordinary=%c0 exact=%c1 string=%c2"
                     :
                     : "s"(&ordinary), "s"(&exact), "s"("abc"));
}
