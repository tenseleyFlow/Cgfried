// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
typedef unsigned long size_t;
void *memcpy(void *restrict, const void *restrict, size_t);

void zero_size_does_not_access_storage(void)
{
    (void)memcpy((void *)0, (const void *)0, 0);
}
