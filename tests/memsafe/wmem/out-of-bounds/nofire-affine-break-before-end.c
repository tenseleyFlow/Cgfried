// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

void nofire_affine_break_before_end(void)
{
    char *p = malloc(8);
    for (int i = 0; i <= 8; i++) {
        if (i == 8)
            break;
        p[i] = 0;
    }
    free(p);
}
