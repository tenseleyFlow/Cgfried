// FLAGS: -fsyntax-only -std=gnu17 -Wno-attributes
/* Both GNU placements describe a pointer to a function returning int. The
 * inner noinline attribute is an optimization hint, so accepting and ignoring
 * it is safe; an attribute with type or symbol semantics is rejected there. */
#define ATTR __attribute__((__noinline__))

int call_both(void *p)
{
    return ((ATTR int (*)(void))p)() + ((int(ATTR *)(void))p)();
}
