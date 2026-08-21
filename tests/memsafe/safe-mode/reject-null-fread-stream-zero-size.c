// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: dereference of a pointer proven to be null
typedef unsigned long size_t;
typedef struct File File;
size_t fread(void *restrict, size_t, size_t, File *restrict);

void zero_bytes_still_need_stream(size_t size, size_t count)
{
    File *stream = 0;

    if (size == 0)
        (void)fread((void *)0, size, count, stream);
}
