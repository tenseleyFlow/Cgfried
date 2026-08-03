// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void consume(char);
void nofire_unknown_index(unsigned long i)
{
    char *p = malloc(8);
    consume(p[i]);
    free(p);
}
