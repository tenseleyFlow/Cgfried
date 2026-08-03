// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);

void indirect_top_nofire(void (*callback)(void *))
{
    void *p = malloc(8);
    callback(p);
}
