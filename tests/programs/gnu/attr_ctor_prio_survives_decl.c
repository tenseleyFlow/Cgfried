// DIVERGES(gcc-8)
// EXIT_CODE: 0
// CHECK: ab
// A DELIBERATE DIVERGENCE FROM GCC, and the only one in this attribute.
//
// When `constructor(N)` lands on a definition whose earlier declaration
// carried no constructor attribute, gcc keeps the constructor-ness and
// SILENTLY DROPS THE PRIORITY -- the function goes in the plain `.init_array`
// as though no number had been written. Measured on gcc 8.5 AND 16.1, so it is
// entrenched rather than a recent regression; clang gets it right.
//
//   void f(void);                                    -> .init_array
//   __attribute__((constructor(101))) void f(void){}
//
//   __attribute__((constructor(101))) void f(void){} -> .init_array.00101
//
// We keep the priority. Attributes here merge as a UNION across declarations
// of one symbol -- the rule `weak` already needed, because musl's weak_alias
// routinely puts the attribute and the definition in different places -- and
// quietly discarding an ordering the author asked for is the failure mode the
// whole attribute tier table exists to prevent.
//
// This EXECUTES rather than checking the section name, because the claim is
// about the order things run in, which is the only thing the priority is for.
extern int printf(const char *, ...);

static int n;
static char seen[4];

/* Both have a prior plain declaration: the shape that loses the priority. */
static void late(void);
static void early(void);

__attribute__((constructor(102))) static void late(void)
{
    seen[n++] = 'b';
}

__attribute__((constructor(101))) static void early(void)
{
    seen[n++] = 'a';
}

int main(void)
{
    seen[n] = '\0';
    printf("%s\n", seen);
    return 0;
}
