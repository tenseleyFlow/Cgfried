/* A comment is not proof that the ownership macros are visible:
 * #include <cgfried/memsafe.h>
 */
void *malloc(unsigned long);

void *make_without_header(void);

void *make_without_header(void)
{
    return malloc(8);
}
