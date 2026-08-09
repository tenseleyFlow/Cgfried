// FLAGS: -fsyntax-only
// WARNING_EXPECTED: attribute ignored
// WARN_COUNT: 2
// On anything but a function there is nothing to run, and gcc WARNS and drops
// the attribute rather than erroring -- so a header that puts one on the wrong
// declaration still compiles. Measured; a first guess would reasonably have
// made it an error, and that would reject code gcc accepts.
//
// The attribute is cleared as well as reported, so nothing downstream can act
// on a flag whose diagnostic already said it was ignored.
//
// WARN_CHECK is LINE-ANCHORED: each one must sit directly above the line that
// warns, which is why these are here rather than in the header block.
// WARN_CHECK: attributes 'constructor' attribute ignored
__attribute__((constructor)) int not_a_function;
// WARN_CHECK: attributes 'destructor' attribute ignored
__attribute__((destructor(101))) int nor_this;

/* A function is fine, including one whose signature gcc does not check: a
 * non-void return and parameters are both accepted silently. */
__attribute__((constructor)) static int returns_something(int unused)
{
    (void)unused;
    return 0;
}

int main(void)
{
    return not_a_function + nor_this + returns_something(0);
}
