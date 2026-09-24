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

void *__memset_chk(void *destination, int value, cgf_size_t length,
                   cgf_size_t object_size)
{
    if (length > object_size)
        abort();
    return memset(destination, value, length);
}
