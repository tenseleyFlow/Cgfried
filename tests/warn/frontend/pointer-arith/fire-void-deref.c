// FLAGS: -fsyntax-only
// DIVERGES(gcc-8): cgf-only-warning=void-ptr-dereference
// WARN_COUNT: 1
void discard_void_dereference(void *p)
{
    // WARN_CHECK: void-ptr-dereference dereferencing 'void *' pointer
    *p;
}
