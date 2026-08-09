// FLAGS: -fsyntax-only
// WARNING_EXPECTED
/* Every position that is NOT an automatic block-scope variable. gcc warns and
 * drops the attribute rather than erroring, so a header carrying one on the
 * wrong declaration still compiles -- and every position below was checked
 * against gcc, which says something about each.
 *
 * The parameter row is why gnu_attrs_any_symbol_property exists. That path
 * warned on a hand-listed three attributes (`packed`, `weak`, `visibility`)
 * and silently dropped every one added since, so this fixture also covers
 * `used`, `alias`, `section`, `constructor` and `aligned` landing there.
 * A silent drop is the exact failure mode docs/gnu-extensions.md exists to
 * prevent, and the parameter position had it for six attributes. */
void f(int *p);

// WARN_CHECK: attributes 'cleanup' attribute ignored
int at_file_scope __attribute__((cleanup(f)));

// WARN_CHECK: attributes attribute ignored on a function parameter
void on_a_param(int p __attribute__((cleanup(f))));

// WARN_CHECK: attributes attribute ignored on a function parameter
void on_a_param_used(int p __attribute__((used)));

// WARN_CHECK: attributes attribute ignored on a function parameter
void on_a_param_section(int p __attribute__((section("s"))));

struct HasMember {
    // WARN_CHECK: attributes 'cleanup' attribute ignored
    int m __attribute__((cleanup(f)));
};

// WARN_CHECK: attributes 'cleanup' attribute ignored
typedef int cleanup_typedef __attribute__((cleanup(f)));

void statics(void)
{
    // WARN_CHECK: attributes 'cleanup' attribute ignored
    static int s __attribute__((cleanup(f)));

    (void)s;
}

void thread_local_too(void)
{
    /* A thread-local outlives every scope exit that could fire the call, so
     * it is the same non-position a static local is. */
    // WARN_CHECK: attributes 'cleanup' attribute ignored
    static _Thread_local int tl __attribute__((cleanup(f)));

    (void)tl;
}
