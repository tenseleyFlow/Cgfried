// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
static void *saved;

static void publish(void *p)
{
    saved = p;
}

void global_escape_nofire(void)
{
    publish(malloc(8));
}
