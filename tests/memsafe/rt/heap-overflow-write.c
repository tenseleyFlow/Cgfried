void *malloc(unsigned long);
void free(void *);
void *memset(void *, int, unsigned long);

int main(void)
{
    char *p = malloc(8);

    /* The library call is intentionally opaque to dereference
     * instrumentation: corrupt the canary and exercise free-time forensics. */
    memset(p, 0x41, 9);
    free(p);
    return 0;
}
