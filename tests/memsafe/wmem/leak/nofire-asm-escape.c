// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
/* An inline-asm template is opaque, so a pointer handed to one has escaped
 * exactly as it would through an unknown call -- see nofire-unknown-call.c,
 * the same rule stated for the other opaque construct.
 *
 * This fired -Wmem-leak until alias.c's mark_escapes learned IR_ASM. The
 * memory-barrier rows the optimizer passes gained at the same time do NOT
 * cover it: those order accesses, while ownership is an escape question, and
 * a pass can be perfectly ordered and still believe it owns the block. The
 * shape is every libc's arch/ directory. */
void *malloc(unsigned long);
void nofire_asm_escape(void)
{
    void *p = malloc(8);

    __asm__ volatile("" : : "r"(p) : "memory");
}
