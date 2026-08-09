// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: inline asm with operands or clobbers is not implemented yet
/* The operand form is REFUSED BY NAME rather than selected approximately.
 *
 * Its constraints need per-operand register allocation -- early-clobber live
 * ranges, matching constraints and fixed pre-coloring -- and putting every
 * operand somewhere plausible instead would assemble, link, and then read the
 * wrong registers. That is the exact failure mode docs/gnu-extensions.md
 * exists to prevent, and it is why this refusal is worth a fixture.
 *
 * HOW WRONG IT WOULD BE, measured on gcc rather than argued: drop the `&`
 * from `"=&r"` in
 *
 *     asm("movl $1, %0\n\taddl %1, %0" : "=&r"(o) : "r"(a))
 *
 * and gcc itself returns 2 where 11 is correct, deterministically at -O0 and
 * -O2, because the output is allowed to share the input's register and the
 * first instruction then destroys the input. That program is the fixture the
 * allocator slice should start from.
 *
 * The refusal lives in LOWERING, not the backend: the backend's only refusal
 * is CGF_ICE, whose text says "this is a bug in cgfried" -- the wrong thing
 * to tell someone who wrote correct C. */
int f(int x)
{
    int y;

    __asm__("movl %1, %0" : "=r"(y) : "r"(x));
    return y;
}
