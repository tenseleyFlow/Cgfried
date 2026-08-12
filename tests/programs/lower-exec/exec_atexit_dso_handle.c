// EXIT_CODE: 0
// glibc's nonshared atexit shim refers to the executable's hidden
// __dso_handle.  Cgfried deliberately omits GCC's crtbegin.o, so its own
// runtime supplies the identity token and the link plan must rescan that
// archive after libc introduces the reference.
int atexit(void (*fn)(void));

static void done(void)
{
}

int main(void)
{
    return atexit(done) == 0 ? 0 : 1;
}
