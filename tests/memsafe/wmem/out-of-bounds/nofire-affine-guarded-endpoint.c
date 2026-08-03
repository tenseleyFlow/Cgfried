// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

void nofire_affine_guarded_endpoint(void)
{
    char *p = malloc(8);
    for (int i = 0; i <= 8; i++) {
        char *q = p + i;
        if (i < 8)
            *q = 0;
    }
    free(p);
}
