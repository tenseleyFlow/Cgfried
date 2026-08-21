void *malloc(unsigned long);
void cgf_safe_check_derive(const void *, long, const void *, unsigned int);

int main(void)
{
    char *p = malloc(8);

    if (!p)
        return 2;
    cgf_safe_check_derive(p, 0x7fffffffffffffffL, p, 99);
    return 0;
}
