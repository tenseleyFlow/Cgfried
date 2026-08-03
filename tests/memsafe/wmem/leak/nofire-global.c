// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void *global_sink;
void nofire_global(void)
{
    global_sink = malloc(8);
}
