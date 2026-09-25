/* libcgf_rt: target-independent checked-memory entry points.
 *
 * Keep these helpers independent of platform fortify internals: every
 * supported target gets the same ABI and overflow behavior, including musl
 * configurations that do not export glibc's __*_chk symbol family. */

typedef unsigned long cgf_size_t;

_Static_assert(sizeof(cgf_size_t) == 8,
               "Cgfried runtime requires a 64-bit size_t carrier");

_Noreturn void abort(void);
void *memset(void *destination, int value, cgf_size_t length);
void *memcpy(void *destination, const void *source, cgf_size_t length);
cgf_size_t strlen(const char *string);

void *__memset_chk(void *destination, int value, cgf_size_t length,
                   cgf_size_t object_size)
{
    if (length > object_size)
        abort();
    return memset(destination, value, length);
}

void *__memcpy_chk(void *destination, const void *source, cgf_size_t length,
                   cgf_size_t object_size)
{
    if (length > object_size)
        abort();
    return memcpy(destination, source, length);
}

char *__stpcpy_chk(char *destination, const char *source,
                   cgf_size_t object_size)
{
    cgf_size_t length = strlen(source);

    /* The terminating null is part of the copy. Writing a string of length
     * N therefore requires strictly more than N destination bytes. */
    if (length >= object_size)
        abort();
    memcpy(destination, source, length + 1);
    return destination + length;
}
