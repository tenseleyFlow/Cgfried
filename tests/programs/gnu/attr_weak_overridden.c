// FLAGS: tests/fixtures/gnu/strong_impl.c
// EXIT_CODE: 7
// `weak` is the first IMPLEMENTED row of docs/gnu-extensions.md, and this
// pins the semantics rather than the directive: a strong definition in
// another TU replaces the weak one, which is the whole point and the reason
// musl's weak_alias pattern works.
//
// Checking the .weak directive alone would pass on a compiler that emitted
// it and got the binding wrong; only the linker's choice proves it. Without
// the attribute this returns 3 and the fixture fails.
__attribute__((weak)) int impl(void)
{
    return 3;
}

int main(void)
{
    return impl();
}
