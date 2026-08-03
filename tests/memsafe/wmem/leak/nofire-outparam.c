// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void nofire_outparam(void **out)
{
    *out = malloc(8);
}
