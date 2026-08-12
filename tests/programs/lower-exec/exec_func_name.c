// EXIT_CODE: 0
// C11 defines __func__ as the enclosing function's name with array semantics;
// GNU __FUNCTION__ is the compatible spelling used by real-world projects.
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int main(void)
{
    const char *a = __func__;

    if (sizeof(__func__) != 5)
        return 1;
    if (!same(__func__, "main"))
        return 2;
    if (!same(__FUNCTION__, "main"))
        return 3;
    /* `__func__` is a function-local static array, not a mergeable string
     * literal.  Both spellings in this function denote the same object, but
     * that object is distinct from an ordinary literal with equal bytes. */
    if (a != __func__ || a != __FUNCTION__)
        return 4;
    if (a == "main")
        return 5;
    return 0;
}
