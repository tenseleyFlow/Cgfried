// FLAGS: --target=x86_64-linux-gnu -m64 -fsyntax-only
/* Cgfried has no 32-bit x86 target: -m64 truthfully constrains the final
 * target to one of the existing x86-64 data models. */
_Static_assert(sizeof(void *) == 8, "-m64 requires the 64-bit data model");
int m64_x86_64;
