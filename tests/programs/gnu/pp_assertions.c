// FLAGS: -std=gnu17 -Wno-deprecated -fsyntax-only
// EXIT_CODE: 0

#define def unused_expansion
#define fail _Static_assert(0, "GNU assertion mismatch")

#assert abc(def)
#assert abc(ghi)
#assert space(s p a c e)

#if !#abc(def) || !#abc(ghi) || !#abc
fail;
#endif

/* Answer tokens are not expanded and internal whitespace is collapsed. */
#if !#space(s p  a  c e) || #space(space)
fail;
#endif

/* This exact adjacency is the GCC torture case that opened the debt bucket. */
#define empty
#assert cpu(m68k)
#if !empty#cpu(m68k)
fail;
#endif

#unassert abc(ghi)
#if !#abc || !#abc(def) || #abc(ghi)
fail;
#endif

#unassert abc
#if #abc || #abc(def) || #abc(ghi)
fail;
#endif

int main(void)
{
    return 0;
}
