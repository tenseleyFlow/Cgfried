#include <cgfried/memsafe.h>

void *malloc(unsigned long);
void free(void *);

void *make_buffer(void);
void consume_buffer(void *buffer);
void maybe_consume(void *buffer, int take);

void *make_buffer(void)
{
    return malloc(16);
}

void consume_buffer(void *buffer)
{
    free(buffer);
}

void maybe_consume(void *buffer, int take)
{
    if (take)
        free(buffer);
}
