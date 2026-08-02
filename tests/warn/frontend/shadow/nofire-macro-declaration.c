// FLAGS: -fsyntax-only -Wshadow
// DIVERGES(gcc-8): CGF suppresses macro-originated shadow declarations by policy.
// WARN_COUNT: 0

#define DECLARE_LOCAL(name) int name = 1

int macro_shadow_name;

void macro_shadow_declaration(void)
{
    DECLARE_LOCAL(macro_shadow_name);
    (void)macro_shadow_name;
}
