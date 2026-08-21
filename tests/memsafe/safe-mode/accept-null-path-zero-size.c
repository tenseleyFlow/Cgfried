// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
typedef unsigned long size_t;
typedef struct File File;
void *memcpy(void *restrict, const void *restrict, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int snprintf(char *restrict, size_t, const char *restrict, ...);
size_t fread(void *restrict, size_t, size_t, File *restrict);
char *strncpy(char *restrict, const char *restrict, size_t);
void bcopy(const void *, void *, size_t);

void single_size_true_edge(size_t size)
{
    if (size == 0) {
        (void)memcpy((void *)0, (const void *)0, size);
        (void)memmove((void *)0, (const void *)0, size);
    }
}

void single_size_false_edge(size_t size)
{
    if (size != 0)
        return;
    (void)memset((void *)0, 0, size);
    (void)snprintf((char *)0, size, "%s", "unused");
}

void first_factor_zero_on_true_edge(size_t size, size_t count, File *stream)
{
    if (size == 0 && stream != 0)
        (void)fread((void *)0, size, count, stream);
}

void second_factor_zero_on_false_edge(size_t size, size_t count, File *stream)
{
    if (count != 0 || stream == 0)
        return;
    (void)fread((void *)0, size, count, stream);
}

void bounded_sources_on_true_edge(size_t size)
{
    if (size == 0)
        (void)strncpy((char *)0, (const char *)0, size);
}

void bounded_sources_on_false_edge(size_t size)
{
    if (size != 0)
        return;
    bcopy((const void *)0, (void *)0, size);
}
